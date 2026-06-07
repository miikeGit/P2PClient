#include "filetransfermanager.h"
#include <QDebug>
#include <QFileInfo>
#include <QJsonDocument>
#include <QCoreApplication>
#include "xxhash.h"

FileTransferManager::FileTransferManager(QObject *parent) : QObject(parent), m_file(this) {
}

FileTransferManager::~FileTransferManager() {
	cleanup();
}

void FileTransferManager::cleanup() {
	if (m_file.isOpen()) m_file.close();
	m_isSending = false;
	m_expectedHash.clear();

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

	QJsonObject metadata;
	metadata["file_name"] = fileName;
	metadata["file_size"] = m_expectedFileSize;
	emit sendJsonCommand(metadata);

	emit transferStarted(fileName);
	m_isSending = true;
}

void FileTransferManager::sendLoop() {
	if (!m_isSending) return;

	constexpr qint64 CHUNK_SIZE = 65536;
	constexpr qint64 HIGH_WATER = 16 * 1024 * 1024;

	while (!m_file.atEnd()) {
		qint64 buffered = m_networkBufferCb ? m_networkBufferCb() : 0;
		if (buffered > HIGH_WATER) {
			return;
		}

		QByteArray chunk = m_file.read(CHUNK_SIZE);
		XXH3_64bits_update(m_hashState, chunk.constData(), chunk.size());
		emit sendBinaryData(chunk);

		qint64 actualBytesSent = qMax((qint64)0, m_file.pos() - buffered);
		if (m_progressTimer.elapsed() > 33) {
			emit progressUpdated(actualBytesSent, m_expectedFileSize);
			m_progressTimer.restart();
		}
		updateSpeedStats(actualBytesSent);
	}

	XXH64_hash_t hashResult = XXH3_64bits_digest(m_hashState);
	QString finalHash = QString::number(hashResult, 16).rightJustified(16, '0');
	qInfo() << "All chunks sent. Final hash:" << finalHash;

	QJsonObject completeMsg;
	completeMsg["action"] = QStringLiteral("transfer_complete");
	completeMsg["file_hash"] = finalHash;
	emit sendJsonCommand(completeMsg);

	cleanup();
	emit transferFinished();
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
			m_progressTimer.start();

			QJsonObject acceptMsg;
			acceptMsg["action"] = QStringLiteral("accept_transfer");
			acceptMsg["resume_offset"] = existingSize;
			emit sendJsonCommand(acceptMsg);
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

					qint64 buffered = m_networkBufferCb ? m_networkBufferCb() : 0;
					m_lastSpeedCheckBytes = qMax((qint64)0, m_file.pos() - buffered);
				}
				m_transferSpeedTimer.start();
				m_progressTimer.start();
				sendLoop();
			}
		} else if (action == "cancel_transfer" || action == "receiver_canceled") {
			cleanup();
			emit transferCanceled();
		} else if (action == "transfer_complete") {
			m_expectedHash = json["file_hash"].toString();
			checkCompletion();
		}
	}
}

void FileTransferManager::handleBinaryChunk(const QByteArray &chunk) {
	if (!m_file.isOpen() || m_isSending) return;

	m_file.write(chunk);
	XXH3_64bits_update(m_hashState, chunk.constData(), chunk.size());
	m_receivedBytes += chunk.size();

	if (m_progressTimer.elapsed() > 33) {
		emit progressUpdated(m_receivedBytes, m_expectedFileSize);
		m_progressTimer.restart();
	}
	updateSpeedStats(m_receivedBytes);
	checkCompletion();
}

void FileTransferManager::cancelTransfer() {
	qWarning() << "Canceling transfer locally";
	const bool wasSending = m_isSending;
	cleanup();
	emit transferCanceled();

	QJsonObject cancelMsg;
	cancelMsg["action"] = wasSending ? QStringLiteral("cancel_transfer") : QStringLiteral("receiver_canceled");
	emit sendJsonCommand(cancelMsg);
}

void FileTransferManager::onPeerDisconnected() {
	qWarning() << "Peer disconnected. Aborting active file transfer";
	cleanup();
	emit transferCanceled();
}

void FileTransferManager::setBackpressure(bool active) {
	if (!active && m_isSending) {
		sendLoop();
	}
}

void FileTransferManager::updateSpeedStats(qint64 currentBytes) {
	qint64 elapsedMs = m_transferSpeedTimer.elapsed();
	if (elapsedMs > 1000) {
		double bytesPerSec = (currentBytes - m_lastSpeedCheckBytes) * 1000.0 / elapsedMs;
		if (bytesPerSec < 0) bytesPerSec = 0;
		emit speedUpdated(bytesPerSec / (1024.0 * 1024.0), (m_expectedFileSize - currentBytes) / qMax(bytesPerSec, 1.0));
		m_transferSpeedTimer.restart();
		m_lastSpeedCheckBytes = currentBytes;
	}
}

void FileTransferManager::checkCompletion() {
	if (m_expectedFileSize > 0 && m_receivedBytes >= m_expectedFileSize && !m_expectedHash.isEmpty()) {
		XXH64_hash_t hashResult = XXH3_64bits_digest(m_hashState);
		QString myHash = QString::number(hashResult, 16).rightJustified(16, '0');

		m_file.close();
		QString savedFilePath = m_file.fileName();
		cleanup();

		if (myHash == m_expectedHash) {
			qInfo() << "File transferred successfully! Hash:" << myHash;
			emit transferFinished();
		} else {
			qCritical() << "Hash mismatch!\nExpected:" << m_expectedHash << "\nGot:" << myHash;
			QFile::remove(savedFilePath);
			emit transferCanceled();
		}
	}
}
