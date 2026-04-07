#include "p2pclient.h"
#include <QSslConfiguration>

using namespace rtc;

P2PClient::P2PClient(const QString &myId, QObject *parent) : QObject(parent), m_myId(myId) {
	SetupMQTT();
}

P2PClient::~P2PClient() {
	closeConnection();
	if (m_mqttClient) {
		m_mqttClient->disconnectFromHost();
		m_mqttClient->deleteLater();
	}
}

void P2PClient::SetupMQTT() {
	const QString hostName{"f15e0b8bb05c484bba3e1e7a82c464c3.s1.eu.hivemq.cloud"};
	const quint16 port{8883};
	const QString username{"throwaway"};
	const QByteArray password{"Throwaway1"};

	m_mqttClient = new QMQTT::Client(hostName, port, QSslConfiguration::defaultConfiguration(), false, this);
	m_mqttClient->setUsername(username);
	m_mqttClient->setPassword(password);
	m_mqttClient->setClientId(m_myId);

	connect(m_mqttClient, &QMQTT::Client::connected, this, &P2PClient::onMQTTConnected);
	connect(m_mqttClient, &QMQTT::Client::received, this, &P2PClient::onMQTTReceived);
}

void P2PClient::connectToBroker() {
	m_mqttClient->connectToHost();
}

void P2PClient::onMQTTConnected() {
	m_mqttClient->subscribe(m_myId, 1);
	emit brokerConnected();
}

void P2PClient::onMQTTReceived(const QMQTT::Message &message) {
	QJsonDocument doc = QJsonDocument::fromJson(message.payload());
	if (!doc.isNull() && doc.isObject()) {
		handleSignalingMessage(doc.object());
	}
}

void P2PClient::SendSignalingMessage(const QJsonObject &message) {
	if (m_targetId.isEmpty())
		return;

	QJsonObject msgCopy = message;
	msgCopy["from"] = m_myId;

	QByteArray payload = QJsonDocument(msgCopy).toJson(QJsonDocument::Compact);

	QMetaObject::invokeMethod(this, [this, payload]() {
		QMQTT::Message mqttMsg(1, m_targetId, payload);
		m_mqttClient->publish(mqttMsg);
	});
}

void P2PClient::SetupWebRTC() {
	Configuration config;
	config.iceServers.emplace_back("stun:stun.l.google.com:19302");

	m_peerConnection = std::make_shared<PeerConnection>(config);

	m_peerConnection->onLocalDescription([this](Description description) {
		QJsonObject msg;
		msg["type"] = QString::fromStdString(description.typeString());
		msg["sdp"] = QString::fromStdString(std::string(description));
		SendSignalingMessage(msg);
	});

	m_peerConnection->onLocalCandidate([this](Candidate candidate) {
		QJsonObject msg;
		msg["type"] = "candidate";
		msg["candidate"] = QString::fromStdString(std::string(candidate));
		msg["mid"] = QString::fromStdString(candidate.mid());
		SendSignalingMessage(msg);
	});

	m_peerConnection->onStateChange([this](PeerConnection::State state) {
		QMetaObject::invokeMethod(this, [this, state]() {
			if (state == PeerConnection::State::Disconnected ||
					state == PeerConnection::State::Failed ||
					state == PeerConnection::State::Closed)
			{
				m_targetId.clear();
				emit connectionClosed();
			}
		});
	});

	m_peerConnection->onDataChannel([this](std::shared_ptr<DataChannel> dataChannel) {
		QMetaObject::invokeMethod(this, [this, dataChannel]() {
			m_dataChannel = dataChannel;
			wireDataChannel();
		});
	});
}

void P2PClient::handleSignalingMessage(const QJsonObject &msg) {
	QString type = msg["type"].toString();

	if (!m_peerConnection) {
		SetupWebRTC();
	}

	if (type == "offer") {
		m_targetId = msg["from"].toString();
		std::string sdp = msg["sdp"].toString().toStdString();
		m_peerConnection->setRemoteDescription(Description(sdp, type.toStdString()));
		m_peerConnection->setLocalDescription();
	} else if (type == "answer") {
		std::string sdp = msg["sdp"].toString().toStdString();
		m_peerConnection->setRemoteDescription(Description(sdp, type.toStdString()));
	} else if (type == "candidate") {
		if (!m_peerConnection) return;

		std::string sdp = msg["candidate"].toString().toStdString();
		std::string mid = msg["mid"].toString().toStdString();

		m_peerConnection->addRemoteCandidate(Candidate(sdp, mid));
	}
}

void P2PClient::wireDataChannel() {
	m_dataChannel->onOpen([this]() {
		QMetaObject::invokeMethod(this, [this]() {
			emit connectionEstablished();
		});
	});

	m_dataChannel->onMessage([this](std::variant<binary, std::string> message) {
		if (std::holds_alternative<std::string>(message)) {
			QString text = QString::fromStdString(std::get<std::string>(message));

			QMetaObject::invokeMethod(this, [this, text]() {
				QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8());
				if (!doc.isNull() && doc.isObject()) {
					emit jsonReceived(doc.object());
				}
			});

		} else if (std::holds_alternative<binary>(message)) {
			auto binChunk = std::get<binary>(message);
			QByteArray chunk(reinterpret_cast<const char *>(binChunk.data()), binChunk.size());
			QMetaObject::invokeMethod(this, [this, chunk]() {
				emit binaryReceived(chunk);
			});
		}
	});
}

void P2PClient::call(const QString &targetId) {
	closeConnection();
	m_targetId = targetId;
	SetupWebRTC();
	m_dataChannel = m_peerConnection->createDataChannel("");
	wireDataChannel();
	m_peerConnection->setLocalDescription();
}

void P2PClient::sendJson(const QJsonObject &json) {
	if (m_dataChannel && m_dataChannel->isOpen()) {
		m_dataChannel->send(QJsonDocument(json).toJson(QJsonDocument::Compact).toStdString());
	}
}

void P2PClient::sendBinary(const QByteArray& data) {
	if (m_dataChannel && m_dataChannel->isOpen()) {
		rtc::binary binChunk(reinterpret_cast<const std::byte*>(data.constData()),
												 reinterpret_cast<const std::byte*>(data.constData()) + data.size());
		m_dataChannel->send(binChunk);
	}
}

void P2PClient::closeConnection() {
	if (m_dataChannel)		m_dataChannel->close();
	if (m_peerConnection) m_peerConnection->close();
}