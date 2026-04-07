#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <memory>
#include <QMainWindow>
#include <qmqtt.h>
#include <rtc/rtc.hpp>
#include <QJsonObject>
#include <QJsonDocument>
#include <QFile>
#include <QFileInfo>
#include <QFileDialog>
#include <QElapsedTimer>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow {
	Q_OBJECT

public:
	MainWindow(QWidget *parent = nullptr);
	~MainWindow();

private slots:
	void on_callButton_clicked();
	void onMQTTConnected();
	void onMQTTReceived(const QMQTT::Message& message);
	void on_sendFileButton_clicked();
	void on_cancelButton_clicked();

private:
	static constexpr qint64 CHUNK_SIZE = 16384;

	std::unique_ptr<Ui::MainWindow> ui;
	QMQTT::Client *m_mqttClient;

	QString m_myId;
	QString m_targetId;

	std::shared_ptr<rtc::PeerConnection> m_peerConnection;
	std::shared_ptr<rtc::DataChannel> m_dataChannel;

	QFile m_incomingFile;
	qint64 m_expectedFileSize = 0;
	qint64 m_receivedBytes = 0;
	qint64 m_lastSpeedCheckBytes = 0;

	QElapsedTimer m_transferSpeedTimer;
	QTimer *m_fileSenderTimer = nullptr;
	bool m_isTransferring = false;

	void SetupMQTT();
	void SetupWebRTC();
	void wireDataChannel();
	void SendSignalingMessage(const QJsonObject& message);
	void handleSignalingMessage(const QJsonObject& message);
};

#endif // MAINWINDOW_H
