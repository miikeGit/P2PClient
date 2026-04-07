#ifndef FILETRANSFERMANAGER_H
#define FILETRANSFERMANAGER_H

#include <QByteArray>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QTimer>

class FileTransferManager : public QObject {
	Q_OBJECT
public:
	explicit FileTransferManager(QObject *parent = nullptr);
	~FileTransferManager();

	void sendFile(const QString &filePath);
	void cancelTransfer();

signals:
	void sendJsonCommand(const QJsonObject &json);
	void sendBinaryData(const QByteArray &data);
	void transferStarted(const QString &fileName, qint64 fileSize);
	void progressUpdated(qint64 current, qint64 total);
	void speedUpdated(double mbps, int eta);
	void transferFinished();
	void transferCanceled();

public slots:
	void handleJsonCommand(const QJsonObject &json);
	void handleBinaryChunk(const QByteArray &chunk);
	void onPeerDisconnected();

private:
	static constexpr qint64 CHUNK_SIZE = 16384;

	QFile m_file;
	QTimer *m_fileSenderTimer = nullptr;
	QElapsedTimer m_transferSpeedTimer;

	qint64 m_expectedFileSize = 0;
	qint64 m_receivedBytes = 0;
	qint64 m_lastSpeedCheckBytes = 0;

	bool m_isSending = false;

	void cleanup();
};

#endif // FILETRANSFERMANAGER_H