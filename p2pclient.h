#ifndef P2PCLIENT_H
#define P2PCLIENT_H

#include <QObject>
#include <QJsonObject>

#include <qmqtt.h>
#include <rtc/rtc.hpp>

#include "appconfig.h"

class P2PClient : public QObject {
	Q_OBJECT
public:
	explicit P2PClient(const AppConfig& config, QObject *parent = nullptr);
	~P2PClient();

	void connectToBroker();
	void call(const QString& targetId);
	void sendJson(const QJsonObject& json);
	void sendBinary(const QByteArray& data);
	void closeConnection();

	QString getMyId() const { return m_myId; }

signals:
	void brokerConnected();
	void connectionEstablished();
	void connectionClosed();
	void jsonReceived(const QJsonObject& json);
	void binaryReceived(const QByteArray& data);
	void connectionStateChanged(int step, QString statusName, int maxSteps = 5);

private slots:
	void onMQTTConnected();
	void onMQTTReceived(const QMQTT::Message& message);

private:
	AppConfig m_config;

	QString m_myId;
	QString m_targetId;

	QMQTT::Client *m_mqttClient;
	std::shared_ptr<rtc::PeerConnection> m_peerConnection;
	std::shared_ptr<rtc::DataChannel> m_dataChannel;

	void setupMQTT();
	void setupWebRTC();
	void wireDataChannel();
	void sendSignalingMessage(const QJsonObject& message);
	void handleSignalingMessage(const QJsonObject& message);
};

#endif // P2PCLIENT_H