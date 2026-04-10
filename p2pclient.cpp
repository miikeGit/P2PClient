#include "p2pclient.h"
#include <QSslConfiguration>

using namespace rtc;

P2PClient::P2PClient(const AppConfig& config, QObject *parent) : QObject(parent), m_config(config) {
	m_myId = QUuid::createUuid().toString(QUuid::WithoutBraces);
	qInfo() << "Local ID:" << m_myId;

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
	m_mqttClient = new QMQTT::Client(m_config.mqtt.host, m_config.mqtt.port, QSslConfiguration::defaultConfiguration(), false, this);
	m_mqttClient->setClientId(m_myId);

	connect(m_mqttClient, &QMQTT::Client::connected, this, &P2PClient::onMQTTConnected);
	connect(m_mqttClient, &QMQTT::Client::received, this, &P2PClient::onMQTTReceived);
}

void P2PClient::connectToBroker() {
	m_mqttClient->connectToHost();
}

void P2PClient::onMQTTConnected() {
	qDebug() << "Connected to broker";
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
	if (m_targetId.isEmpty()) {
		qWarning() << "Cannot send signaling message: Target ID is empty!";
		return;
	}

	QJsonObject msgCopy = message;
	msgCopy["from"] = m_myId;

	QByteArray payload = QJsonDocument(msgCopy).toJson(QJsonDocument::Compact);
	qDebug() << "Sending" << message["type"].toString() << "to target:" << m_targetId;

	QMetaObject::invokeMethod(this, [this, payload]() {
		QMQTT::Message mqttMsg(1, m_targetId, payload);
		m_mqttClient->publish(mqttMsg);
	});
}

void P2PClient::SetupWebRTC() {
	qInfo() << "Initializing WebRTC PeerConnection...";
	Configuration config;

	for (const auto& server : m_config.iceServers) {
		qDebug() << "Adding ICE server: " << server.urls;
		IceServer iceServer(server.urls.toStdString());
		if (!server.username.isEmpty()) iceServer.username = server.username.toStdString();
		if (!server.password.isEmpty()) iceServer.password = server.password.toStdString();
		config.iceServers.push_back(iceServer);
	}

	m_peerConnection = std::make_shared<PeerConnection>(config);
	m_peerConnection->onLocalDescription([this](Description description) {
		qInfo() << "Generated Local SDP Description. Type: " << QString::fromStdString(description.typeString());
		qDebug() << "Local SDP Payload:\n" << QString::fromStdString(std::string(description));

		QJsonObject msg;
		msg["type"] = QString::fromStdString(description.typeString());
		msg["sdp"] = QString::fromStdString(std::string(description));
		SendSignalingMessage(msg);
	});

	m_peerConnection->onLocalCandidate([this](Candidate candidate) {
		qDebug() << "Got local ICE candidate:" << QString::fromStdString(std::string(candidate));

		QJsonObject msg;
		msg["type"] = "candidate";
		msg["candidate"] = QString::fromStdString(std::string(candidate));
		msg["mid"] = QString::fromStdString(candidate.mid());
		SendSignalingMessage(msg);
	});

	m_peerConnection->onStateChange([this](PeerConnection::State state) {
		QMetaObject::invokeMethod(this, [this, state]() {
			QString stateStr;
			switch(state) {
				case PeerConnection::State::New:					stateStr = "New";					 break;
				case PeerConnection::State::Connecting:		stateStr = "Connecting";   break;
				case PeerConnection::State::Connected:		stateStr = "Connected";    break;
				case PeerConnection::State::Disconnected: stateStr = "Disconnected"; break;
				case PeerConnection::State::Failed:				stateStr = "Failed";			 break;
				case PeerConnection::State::Closed:				stateStr = "Closed";			 break;
				default:																	stateStr = "Unknown";			 break;
			}
			qInfo() << "PeerConnection state changed to" << stateStr;

			if (!m_targetId.isEmpty() && (state == PeerConnection::State::Disconnected ||
																		state == PeerConnection::State::Failed ||
																		state == PeerConnection::State::Closed)) {
				m_targetId.clear();
				emit connectionClosed();
			}
		});
	});

	m_peerConnection->onDataChannel([this](std::shared_ptr<DataChannel> dataChannel) {
		qInfo() << "Remote peer created a DataChannel. Wiring it up...";
		QMetaObject::invokeMethod(this, [this, dataChannel]() {
			m_dataChannel = dataChannel;
			wireDataChannel();
		});
	});
}

void P2PClient::handleSignalingMessage(const QJsonObject &msg) {
	QString type = msg["type"].toString();
	qInfo() << "Got incoming message of type -" << type;

	if (!m_peerConnection) {
		qDebug() << "PeerConnection not initialized yet. Setting up WebRTC...";
		SetupWebRTC();
	}

	if (type == "offer") {
		m_targetId = msg["from"].toString();
		std::string sdp = msg["sdp"].toString().toStdString();
		qDebug() << "Received remote offer SDP:\n" << QString::fromStdString(sdp);

		m_peerConnection->setRemoteDescription(Description(sdp, type.toStdString()));
		m_peerConnection->setLocalDescription();
	} else if (type == "answer") {
		std::string sdp = msg["sdp"].toString().toStdString();
		qDebug() << "Received remote answer SDP:\n" << QString::fromStdString(sdp);

		m_peerConnection->setRemoteDescription(Description(sdp, type.toStdString()));
	} else if (type == "candidate") {
		if (!m_peerConnection) return;

		std::string sdp = msg["candidate"].toString().toStdString();
		std::string mid = msg["mid"].toString().toStdString();
		qDebug() << "Received remote ICE candidate:" << QString::fromStdString(sdp);

		m_peerConnection->addRemoteCandidate(Candidate(sdp, mid));
	}
	else {
		qWarning() << "Unknown signaling message type received:" << type;
	}
}

void P2PClient::wireDataChannel() {
	m_dataChannel->onOpen([this]() {
		QMetaObject::invokeMethod(this, [this]() {
			qInfo() << "WebRTC DataChannel opened successfully!";
			emit connectionEstablished();
		});
	});

	m_dataChannel->onClosed([this]() {
		QMetaObject::invokeMethod(this, [this]() {
			qWarning() << "WebRTC DataChannel closed!";
		});
	});

	m_dataChannel->onMessage([this](std::variant<binary, std::string> message) {
		if (std::holds_alternative<std::string>(message)) {
			QString text = QString::fromStdString(std::get<std::string>(message));
			qDebug() << "Received JSON command over DataChannel. Length:" << text.length();

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
	if (targetId == m_myId) {
		qWarning() << "Attempted to call self!";
		return;
	}

	qInfo() << "Initiating call to target:" << targetId;
	closeConnection();
	m_targetId = targetId;
	SetupWebRTC();
	m_dataChannel = m_peerConnection->createDataChannel("");
	wireDataChannel();
	m_peerConnection->setLocalDescription();
}

void P2PClient::sendJson(const QJsonObject &json) {
	if (m_dataChannel && m_dataChannel->isOpen()) {
		qDebug() << "Sending JSON over DataChannel:" << json["action"].toString() << json["file_name"].toString();
		m_dataChannel->send(QJsonDocument(json).toJson(QJsonDocument::Compact).toStdString());
	}
	else {
		qWarning() << "Failed to send JSON: DataChannel is not open!";
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
	qInfo() << "Closing WebRTC connection and DataChannel...";
	if (m_dataChannel)		m_dataChannel->close();
	if (m_peerConnection) m_peerConnection->close();
}