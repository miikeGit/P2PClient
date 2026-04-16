#ifndef FILETRANSFERMANAGER_H
#define FILETRANSFERMANAGER_H

#include <QByteArray>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QTimer>
#include <functional>
#include "xxhash.h"

class FileTransferManager : public QObject {
	Q_OBJECT
public:
	explicit FileTransferManager(QObject *parent = nullptr);
	~FileTransferManager();

	void sendFile(const QString &filePath);
	void cancelTransfer();
	void togglePause();
	void setDownloadPath(const QString &path) { m_downloadPath = path; }
	void setNetworkBufferCallback(std::function<qint64()> cb) { m_networkBufferCb = std::move(cb); }

signals:
	void sendJsonCommand(const QJsonObject &json);
	void sendBinaryData(const QByteArray &data);
	void transferStarted(const QString &fileName);
	void progressUpdated(qint64 current, qint64 total);
	void speedUpdated(double mbps, int eta);
	void transferFinished();
	void transferCanceled();
	void transferPaused(bool paused);

public slots:
	void handleJsonCommand(const QJsonObject &json);
	void handleBinaryChunk(const QByteArray &chunk);
	void onPeerDisconnected();
	void setSpeedLimit(int kbps);
	void setBackpressure(bool active);

private slots:
	void sendNextChunk();

private:
	std::function<qint64()> m_networkBufferCb;
	XXH3_state_t* m_hashState = nullptr;
	QString m_downloadPath;

	QFile m_file;
	QTimer m_fileSenderTimer;
	QElapsedTimer m_transferSpeedTimer;

	qint64 m_expectedFileSize = 0;
	qint64 m_receivedBytes = 0;
	qint64 m_lastSpeedCheckBytes = 0;
	qint64 m_speedLimitKbps = 0;

	bool m_isSending = false;
	bool m_isPaused = false;
	bool m_backpressure = false;

	void cleanup();
	void applyPauseState();
	void updateSpeedStats(qint64 currentBytes);
	QString calculateFileHash();
};

#endif // FILETRANSFERMANAGER_H