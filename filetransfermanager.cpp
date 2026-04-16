#include "filetransfermanager.h"
#include <QDebug>
#include <QFileInfo>
#include <QJsonDocument>
#include <QCoreApplication>
#include "xxhash.h"

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

	if (m_hashState) {
		XXH3_freeState(m_hashState);
		m_hashState = nullptr;
	}
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
	m_hashState = XXH3_createState();
	XXH3_64bits_reset(m_hashState);

	emit sendJsonCommand({
		{"file_name", fileName},
		{"file_size", m_expectedFileSize}
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
		XXH64_hash_t hashResult = XXH3_64bits_digest(m_hashState);
		QString finalHash = QString::number(hashResult, 16).rightJustified(16, '0');
		qInfo() << "All chunks sent. Final hash:" << finalHash;

		emit sendJsonCommand({{"action", "transfer_complete"},
													{"file_hash", finalHash}});

		cleanup();
		emit transferFinished();
		return;
	}

	constexpr qint64 CHUNK_SIZE = 16384;
	QByteArray chunk = m_file.read(CHUNK_SIZE);
	XXH3_64bits_update(m_hashState, chunk.constData(), chunk.size());
	emit sendBinaryData(chunk);

	qint64 bytesSent = m_file.pos();
	emit progressUpdated(bytesSent, m_expectedFileSize);
	updateSpeedStats(bytesSent);
}

void FileTransferManager::handleJsonCommand(const QJsonObject &json) {
	if (json.contains("file_name")) {
		cleanup();
		m_expectedFileSize = json["file_size"].toVariant().toLongLong();
		m_receivedBytes = 0;

		QString fileName = json["file_name"].toString();
		qInfo() << "Incoming file metadata. Name:" << fileName << "| Expected size:" << m_expectedFileSize;

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
			m_hashState = XXH3_createState();
			XXH3_64bits_reset(m_hashState);

			if (existingSize > 0) {
				m_file.seek(0);
				const qint64 RESUME_CHUNK = 10 * 1024 * 1024;
				while (m_file.pos() < existingSize) {
					QByteArray data = m_file.read(RESUME_CHUNK);
					if (data.isEmpty()) break;
					XXH3_64bits_update(m_hashState, data.constData(), data.size());
					QCoreApplication::processEvents();
				}
			}

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
		} else {
			qCritical() << "Failed to create local file for downloading at:" << savePath;
		}
	} else if (json.contains("action")) {
		QString action = json["action"].toString();
		qInfo() << "Received action command from peer:" << action;

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
		} else if (action == "cancel_transfer" || action == "receiver_canceled") {
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
		} else if (action == "transfer_complete") {
			QString expectedHash = json["file_hash"].toString();
			XXH64_hash_t hashResult = XXH3_64bits_digest(m_hashState);
			QString myHash = QString::number(hashResult, 16).rightJustified(16, '0');

			m_file.close();
			QString savedFilePath = m_file.fileName();
			cleanup();

			if (myHash == expectedHash) {
				qInfo() << "File transferred successfully! Hash:" << myHash;
				emit transferFinished();
			} else {
				qCritical() << "Hash mismatch!\nExpected:" << expectedHash << "\nGot:" << myHash;
				QFile::remove(savedFilePath);
				emit transferCanceled();
			}
		}
	}
}

void FileTransferManager::handleBinaryChunk(const QByteArray &chunk) {
	if (!m_file.isOpen() || m_isSending) return;

	m_file.write(chunk);
	XXH3_64bits_update(m_hashState, chunk.constData(), chunk.size());
	m_receivedBytes += chunk.size();

	emit progressUpdated(m_receivedBytes, m_expectedFileSize);
	updateSpeedStats(m_receivedBytes);
}

void FileTransferManager::cancelTransfer() {
	qWarning() << "Canceling transfer locally";
	cleanup();
	emit transferCanceled();
	emit sendJsonCommand({{"action", "cancel_transfer"}});
}

void FileTransferManager::onPeerDisconnected() {
	qWarning() << "Peer disconnected. Aborting active file transfers";
	cleanup();
	emit transferCanceled();
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