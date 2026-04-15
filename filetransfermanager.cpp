#include "filetransfermanager.h"
#include <QDebug>
#include <QFileInfo>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QCryptographicHash>
#include <QCoreApplication>

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
	if (!m_file.open(QIODevice::ReadOnly)) {
		qCritical() << "Failed to open file for sending:" << filePath;
		return;
	}

	QFileInfo fileInfo(m_file);
	m_expectedFileSize = fileInfo.size();

	qInfo() << "Preparing to send file:" << fileInfo.fileName() << "| Size:" << m_expectedFileSize << "bytes";
	m_cancelTransfer = false;

	QString fileHash = calculateSha256();

	if (m_cancelTransfer) {
		qWarning() << "Send file aborted during hash calculation.";
		return;
	}

	qDebug() << "Calculated SHA-256 for outgoing file:" << fileHash;
	m_file.seek(0);

	QJsonObject meta;
	meta["file_name"] = fileInfo.fileName();
	meta["file_size"] = m_expectedFileSize;
	meta["file_hash"] = fileHash;

	emit sendJsonCommand(meta);
	emit transferStarted(fileInfo.fileName());

	m_isSending = true;
	m_lastSpeedCheckBytes = 0;
	m_transferSpeedTimer.start();

	m_fileSenderTimer = new QTimer(this);
	connect(m_fileSenderTimer, &QTimer::timeout, this, [this]() {
		if (m_file.atEnd()) {
			qInfo() << "All file chunks sent successfully.";
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
	setSpeedLimit(m_speedLimitKbps);
}

void FileTransferManager::handleJsonCommand(const QJsonObject &json) {
	if (json.contains("file_name")) {
		cleanup();
		m_expectedFileSize = json["file_size"].toVariant().toLongLong();
		m_expectedHash = json["file_hash"].toString();
		m_receivedBytes = 0;

		qInfo() << "Incoming file metadata. "
							 "Name: " << json["file_name"].toString() <<
							 "\nExpected size: " << m_expectedFileSize <<
							 "\nExpected hash: " << m_expectedHash;

		QString savePath = m_downloadPath + "/" + json["file_name"].toString();
		m_file.setFileName(savePath);

		qint64 existingSize = 0;
		QFileInfo fi(savePath);
		if (fi.exists()) {
			if (fi.size() <= m_expectedFileSize) {
				existingSize = fi.size();
				qInfo() << "Found existing file, size:" << existingSize << "bytes. Resuming...";
			} else {
				QFile::remove(savePath);
			}
		}

		if (m_file.open(QIODevice::Append)) {
			m_receivedBytes = existingSize;
			qDebug() << "Successfully opened local file for downloading at:" << savePath;
			emit transferStarted(json["file_name"].toString());
			emit progressUpdated(m_receivedBytes, m_expectedFileSize);
			m_lastSpeedCheckBytes = m_receivedBytes;
			m_transferSpeedTimer.start();

			QJsonObject reply;
			reply["action"] = "accept_transfer";
			reply["resume_offset"] = existingSize;
			emit sendJsonCommand(reply);

			if (m_receivedBytes >= m_expectedFileSize) {
				handleBinaryChunk(QByteArray()); 
			}
		}
		else {
			qCritical() << "Failed to create local file for downloading at:" << savePath;
		}
	}
	else if (json.contains("action")) {
		QString action = json["action"].toString();
		
		if (action == "accept_transfer") {
			if (m_isSending && m_fileSenderTimer) {
				qint64 offset = json["resume_offset"].toVariant().toLongLong();
				if (offset >= 0 && offset <= m_expectedFileSize) {
					m_file.seek(offset);
					qInfo() << "Resuming transfer from offset:" << offset;
					emit progressUpdated(offset, m_expectedFileSize);
					m_lastSpeedCheckBytes = offset;
				}
				m_transferSpeedTimer.start();
				setSpeedLimit(m_speedLimitKbps);
				m_fileSenderTimer->start();
			}
		}
		else {
			qWarning() << "Received action command from peer:" << action;
			if (action == "cancel_transfer" || action == "receiver_canceled") {
				cleanup();
				emit transferCanceled();
			}
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
		qInfo() << "All bytes received. Verifying file integrity...";
		QString savedFilePath = m_file.fileName();

		m_file.close();
		if (!m_file.open(QIODevice::ReadOnly)) {
			qCritical() << "Failed to reopen file for hash check";
			emit transferCanceled();
			return;
		}

		m_cancelTransfer = false;

		QString downloadedHash = calculateSha256();

		if (m_cancelTransfer) {
			QFile::remove(savedFilePath);
			return;
		}

		cleanup();

		if (downloadedHash == m_expectedHash) {
			qInfo() << "File hashes match -" << downloadedHash;
			emit transferFinished();
		} else {
			qCritical() << "Hash mismatch!"
										 "\nExpected:" << m_expectedHash <<
										 "\nGot:     " << downloadedHash <<
										 "\nDeleting corrupted file";
			QFile::remove(savedFilePath);
			emit transferCanceled();
		}
	}
}

void FileTransferManager::cancelTransfer() {
	qWarning() << "Canceling transfer locally";
	m_cancelTransfer = true;
	QJsonObject cancelMsg;
	if (m_isSending) {
		cancelMsg["action"] = "cancel_transfer";
		cleanup();
		emit transferCanceled();
	}
	else if (m_file.isOpen()) {
		if (m_file.openMode() & (QIODevice::WriteOnly | QIODevice::Append)) {
			cancelMsg["action"] = "receiver_canceled";
		} else {
			cancelMsg["action"] = "cancel_transfer";
		}
		cleanup();
		emit transferCanceled();
	}
	emit sendJsonCommand(cancelMsg);
}

void FileTransferManager::onPeerDisconnected() {
	qWarning() << "Peer disconnected. Aborting active file transfers";
	cleanup();
	emit transferCanceled();
}

QString FileTransferManager::calculateSha256() {
	QCryptographicHash hash(QCryptographicHash::Sha256);
	qint64 bytesRead = 0;
	const qint64 HASH_CHUNK_SIZE = 10 * 1024 * 1024;

	while (!m_file.atEnd() && !m_cancelTransfer) {
		QByteArray chunk = m_file.read(HASH_CHUNK_SIZE);
		hash.addData(chunk);
		bytesRead += chunk.size();

		emit hashProgressUpdated(bytesRead, m_expectedFileSize);
		QCoreApplication::processEvents();
	}

	return QString(hash.result().toHex());
}

void FileTransferManager::setSpeedLimit(int kbps) {
	m_speedLimitKbps = kbps;

	if (m_fileSenderTimer) {
		if (m_speedLimitKbps > 0) {
			int delayMs = (16.0 / m_speedLimitKbps) * 1000;
			m_fileSenderTimer->setInterval(qMax(1, delayMs));
		} else {
			m_fileSenderTimer->setInterval(0); // no limit
		}
	}
}