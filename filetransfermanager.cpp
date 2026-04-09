#include "filetransfermanager.h"
#include <QDebug>
#include <QFileInfo>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QCryptographicHash>

FileTransferManager::FileTransferManager(QObject *parent) : QObject(parent) {}
FileTransferManager::~FileTransferManager() {
	cleanup();
}

void FileTransferManager::cleanup() {
	if (m_fileSenderTimer) {
		m_fileSenderTimer->stop();
		m_fileSenderTimer->deleteLater();
		m_fileSenderTimer = nullptr;
	}
	if (m_file.isOpen()) m_file.close();
	m_isSending = false;
}

void FileTransferManager::sendFile(const QString &filePath) {
	cleanup();
	m_file.setFileName(filePath);
	if (!m_file.open(QIODevice::ReadOnly)) return;

	QFileInfo fileInfo(m_file);
	m_expectedFileSize = fileInfo.size();

	QJsonObject meta;
	meta["file_name"] = fileInfo.fileName();
	meta["file_size"] = m_expectedFileSize;
	meta["file_hash"] = calculateSha256(filePath);

	emit sendJsonCommand(meta);
	emit transferStarted(fileInfo.fileName(), m_expectedFileSize);

	m_isSending = true;
	m_lastSpeedCheckBytes = 0;
	m_transferSpeedTimer.start();

	m_fileSenderTimer = new QTimer(this);
	connect(m_fileSenderTimer, &QTimer::timeout, this, [this]() {
		if (m_file.atEnd()) {
			cleanup();
			emit transferFinished();
			return;
		}

		QByteArray chunk = m_file.read(CHUNK_SIZE);
		emit sendBinaryData(chunk);

		qint64 bytesSent = m_file.pos();
		emit progressUpdated(bytesSent, m_expectedFileSize);

		qint64 elapsedMs = m_transferSpeedTimer.elapsed();
		if (elapsedMs > 1000) {
			double bytesPerSec = (bytesSent - m_lastSpeedCheckBytes) * 1000.0 / elapsedMs;
			double MBps = bytesPerSec / (1024.0 * 1024.0);

			int eta = (m_expectedFileSize - bytesSent) / bytesPerSec;
			emit speedUpdated(MBps, eta);
			m_transferSpeedTimer.restart();
			m_lastSpeedCheckBytes = bytesSent;
		}
	});
	m_fileSenderTimer->start(0);
}

void FileTransferManager::handleJsonCommand(const QJsonObject &json) {
	if (json.contains("file_name")) {
		cleanup();
		m_expectedFileSize = json["file_size"].toVariant().toLongLong();
		m_expectedHash = json["file_hash"].toString();
		m_receivedBytes = 0;

		QString savePath = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation) + "/" + json["file_name"].toString();
		m_file.setFileName(savePath);

		if (m_file.open(QIODevice::WriteOnly)) {
			emit transferStarted(json["file_name"].toString(), m_expectedFileSize);
			m_lastSpeedCheckBytes = 0;
			m_transferSpeedTimer.start();
		}
	}
	else if (json.contains("action")) {
		QString action = json["action"].toString();
		if (action == "cancel_transfer" || action == "receiver_canceled") {
			if (!m_isSending && m_file.isOpen()) m_file.remove();
			cleanup();
			emit transferCanceled();
		}
	}
}

void FileTransferManager::handleBinaryChunk(const QByteArray &chunk) {
	if (!m_file.isOpen() || m_isSending) return;

	m_file.write(chunk);
	m_receivedBytes += chunk.size();

	emit progressUpdated(m_receivedBytes, m_expectedFileSize);

	qint64 elapsedMs = m_transferSpeedTimer.elapsed();
	if (elapsedMs > 1000) {
		double bytesPerSec = (m_receivedBytes - m_lastSpeedCheckBytes) * 1000.0 / elapsedMs;
		double MBps = bytesPerSec / (1024.0 * 1024.0);
		int eta = (m_expectedFileSize - m_receivedBytes) / bytesPerSec;

		emit speedUpdated(MBps, eta);
		m_transferSpeedTimer.restart();
		m_lastSpeedCheckBytes = m_receivedBytes;
	}

	if (m_receivedBytes >= m_expectedFileSize) {
		QString savedFilePath = m_file.fileName();
		cleanup();
		QString downloadedHash = calculateSha256(savedFilePath);

		if (downloadedHash == m_expectedHash) {
			emit transferFinished();
		} else {
			QFile::remove(savedFilePath);
			emit transferCanceled();
		}
	}
}

void FileTransferManager::cancelTransfer() {
	QJsonObject cancelMsg;
	if (m_isSending) {
		cancelMsg["action"] = "cancel_transfer";
		cleanup();
		emit transferCanceled();
	}
	else if (m_file.isOpen()) {
		cancelMsg["action"] = "receiver_canceled";
		m_file.remove();
		cleanup();
		emit transferCanceled();
	}
	emit sendJsonCommand(cancelMsg);
}

void FileTransferManager::onPeerDisconnected() {
	if (m_file.isOpen() && !m_isSending) m_file.remove();
	cleanup();
	emit transferCanceled();
}

QString FileTransferManager::calculateSha256(const QString &filePath) {
	QFile file(filePath);
	if (!file.open(QIODevice::ReadOnly)) return QString();

	QCryptographicHash hash(QCryptographicHash::Sha256);
	if (hash.addData(&file)) return QString(hash.result().toHex());
	return QString();
}