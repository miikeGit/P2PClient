#include "filetransfermanager.h"
#include <QDebug>
#include <QFileInfo>
#include <QJsonDocument>
#include <QCryptographicHash>
#include <QCoreApplication>

FileTransferManager::FileTransferManager(QObject *parent) : QObject(parent) {
	connect(&m_fileSenderTimer, &QTimer::timeout, this, &FileTransferManager::sendNextChunk);
}

FileTransferManager::~FileTransferManager() {
	cleanup();
}

void FileTransferManager::cleanup() {
	m_fileSenderTimer.stop();
	if (m_file.isOpen()) m_file.close();
	m_isSending = false;
	m_isPaused = false;
}

void FileTransferManager::sendFile(const QString &filePath) {
	cleanup();
	m_file.setFileName(filePath);
	if (!m_file.open(QIODevice::ReadOnly)) {
		qCritical() << "Failed to open file for sending:" << filePath;
		return;
	}

	m_expectedFileSize = m_file.size();
	QString fileName = QFileInfo(m_file).fileName();

	qInfo() << "Preparing to send file:" << fileName << "| Size:" << m_expectedFileSize << "bytes";
	m_cancelTransfer = false;

	QString fileHash = calculateSha256();

	if (m_cancelTransfer) {
		qWarning() << "Send file aborted during hash calculation.";
		return;
	}

	qDebug() << "Calculated SHA-256 for outgoing file:" << fileHash;
	m_file.seek(0);

	emit sendJsonCommand({
		{"file_name", fileName},
		{"file_size", m_expectedFileSize},
		{"file_hash", fileHash}
	});
	emit transferStarted(fileName);

	m_isSending = true;
	m_lastSpeedCheckBytes = 0;
	m_transferSpeedTimer.start();

	setSpeedLimit(m_speedLimitKbps);
	m_fileSenderTimer.start();
}

void FileTransferManager::sendNextChunk() {
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
	updateSpeedStats(bytesSent);
}

void FileTransferManager::handleJsonCommand(const QJsonObject &json) {
	if (json.contains("file_name")) {
		cleanup();
		m_expectedFileSize = json["file_size"].toVariant().toLongLong();
		m_expectedHash = json["file_hash"].toString();
		m_receivedBytes = 0;

		QString fileName = json["file_name"].toString();
		qInfo() << "Incoming file metadata. Name:" << fileName << "| Expected size:" << m_expectedFileSize << "| Expected hash:" << m_expectedHash;

		QString savePath = m_downloadPath + "/" + fileName;
		m_file.setFileName(savePath);

		qint64 existingSize = m_file.size();
		if (existingSize > m_expectedFileSize) {
			QFile::remove(savePath);
			existingSize = 0;
		} else if (existingSize > 0) {
			qInfo() << "Found existing file, size:" << existingSize << "bytes. Resuming...";
		}

		if (m_file.open(QIODevice::Append)) {
			m_receivedBytes = existingSize;
			qDebug() << "Successfully opened local file for downloading at:" << savePath;
			emit transferStarted(fileName);
			emit progressUpdated(m_receivedBytes, m_expectedFileSize);
			m_lastSpeedCheckBytes = m_receivedBytes;
			m_transferSpeedTimer.start();

			emit sendJsonCommand({
				{"action", "accept_transfer"},
				{"resume_offset", existingSize}
			});

			if (m_receivedBytes >= m_expectedFileSize) {
				handleBinaryChunk(QByteArray()); 
			}
		} else {
			qCritical() << "Failed to create local file for downloading at:" << savePath;
		}
	} else if (json.contains("action")) {
		QString action = json["action"].toString();
		
		if (action == "accept_transfer") {
			if (m_isSending) {
				qint64 offset = json["resume_offset"].toVariant().toLongLong();
				if (offset >= 0 && offset <= m_expectedFileSize) {
					m_file.seek(offset);
					qInfo() << "Resuming transfer from offset:" << offset;
					emit progressUpdated(offset, m_expectedFileSize);
					m_lastSpeedCheckBytes = offset;
				}
				m_transferSpeedTimer.start();
				setSpeedLimit(m_speedLimitKbps);
				m_fileSenderTimer.start();
			}
		} else {
			qWarning() << "Received action command from peer:" << action;
			if (action == "cancel_transfer" || action == "receiver_canceled") {
				cleanup();
				emit transferCanceled();
			} else if (action == "pause_transfer") {
				m_isPaused = true;
				applyPauseState();
				emit transferPaused(true);
			} else if (action == "resume_transfer") {
				m_isPaused = false;
				applyPauseState();
				emit transferPaused(false);
			}
		}
	}
}

void FileTransferManager::handleBinaryChunk(const QByteArray &chunk) {
	if (!m_file.isOpen() || m_isSending) return;

	m_file.write(chunk);
	m_receivedBytes += chunk.size();

	emit progressUpdated(m_receivedBytes, m_expectedFileSize);
	updateSpeedStats(m_receivedBytes);

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
			qCritical() << "Hash mismatch!\nExpected:" << m_expectedHash << "\nGot:     " << downloadedHash << "\nDeleting corrupted file";
			QFile::remove(savedFilePath);
			emit transferCanceled();
		}
	}
}

void FileTransferManager::cancelTransfer() {
	qWarning() << "Canceling transfer locally";
	m_cancelTransfer = true;
	cleanup();
	emit transferCanceled();
	emit sendJsonCommand({{"action", "cancel_transfer"}});
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
	m_fileSenderTimer.setInterval(m_speedLimitKbps > 0 ? qMax(1, (int)((16.0 / m_speedLimitKbps) * 1000)) : 0);
}

void FileTransferManager::togglePause() {
	m_isPaused = !m_isPaused;
	emit sendJsonCommand({{"action", m_isPaused ? "pause_transfer" : "resume_transfer"}});

	applyPauseState();
	emit transferPaused(m_isPaused);
}

void FileTransferManager::applyPauseState() {
	if (m_isSending) {
		if (m_isPaused) {
			m_fileSenderTimer.stop();
		} else {
			m_transferSpeedTimer.restart();
			m_lastSpeedCheckBytes = m_file.pos();
			m_fileSenderTimer.start();
		}
	} else if (!m_isPaused) {
		m_transferSpeedTimer.restart();
		m_lastSpeedCheckBytes = m_receivedBytes;
	}
}

void FileTransferManager::updateSpeedStats(qint64 currentBytes) {
	qint64 elapsedMs = m_transferSpeedTimer.elapsed();
	if (elapsedMs > 1000) {
		double bytesPerSec = (currentBytes - m_lastSpeedCheckBytes) * 1000.0 / elapsedMs;
		emit speedUpdated(bytesPerSec / (1024.0 * 1024.0), (m_expectedFileSize - currentBytes) / bytesPerSec);
		m_transferSpeedTimer.restart();
		m_lastSpeedCheckBytes = currentBytes;
	}
}