#ifndef P2PCLIENT_H
#define P2PCLIENT_H

#include <QObject>
#include <QJsonObject>
#include <QJsonDocument>
#include <QByteArray>
#include <QString>

#include <qmqtt.h>
#include <rtc/rtc.hpp>

class P2PClient : public QObject {
	Q_OBJECT
public:
	explicit P2PClient(const QString& myId, QObject *parent = nullptr);
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

private slots:
	void onMQTTConnected();
	void onMQTTReceived(const QMQTT::Message& message);

private:
	QString m_myId;
	QString m_targetId;

	QMQTT::Client *m_mqttClient;
	std::shared_ptr<rtc::PeerConnection> m_peerConnection;
	std::shared_ptr<rtc::DataChannel> m_dataChannel;

	void SetupMQTT();
	void SetupWebRTC();
	void wireDataChannel();
	void SendSignalingMessage(const QJsonObject& message);
	void handleSignalingMessage(const QJsonObject& message);
};

#endif // P2PCLIENT_H