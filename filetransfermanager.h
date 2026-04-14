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
	void setDownloadPath(const QString &path) { m_downloadPath = path; }

signals:
	void sendJsonCommand(const QJsonObject &json);
	void sendBinaryData(const QByteArray &data);
	void transferStarted(const QString &fileName);
	void progressUpdated(qint64 current, qint64 total);
	void speedUpdated(double mbps, int eta);
	void transferFinished();
	void transferCanceled();
	void hashProgressUpdated(qint64 current, qint64 total);

public slots:
	void handleJsonCommand(const QJsonObject &json);
	void handleBinaryChunk(const QByteArray &chunk);
	void onPeerDisconnected();
	void setSpeedLimit(int kbps);

private:
	QString m_expectedHash;
	QString m_downloadPath;

	static constexpr qint64 CHUNK_SIZE = 16384;

	QFile m_file;
	QTimer *m_fileSenderTimer = nullptr;
	QElapsedTimer m_transferSpeedTimer;

	qint64 m_expectedFileSize = 0;
	qint64 m_receivedBytes = 0;
	qint64 m_lastSpeedCheckBytes = 0;
	qint64 m_speedLimitKbps = 0;

	bool m_isSending = false;
	bool m_cancelTransfer = false;

	void cleanup();
	QString calculateSha256();
};

#endif // FILETRANSFERMANAGER_H