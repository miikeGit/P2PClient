#include "p2pclient.h"
#include <QSslConfiguration>

using namespace rtc;

P2PClient::P2PClient(const AppConfig& config, QObject *parent) : QObject(parent), m_config(config) {
	m_myId = QUuid::createUuid().toString(QUuid::WithoutBraces);
	qInfo() << "Local ID:" << m_myId;

	setupMQTT();
}

P2PClient::~P2PClient() {
	closeConnection();
	m_mqttClient->disconnectFromHost();
}

void P2PClient::setupMQTT() {
	m_mqttClient = new QMQTT::Client(m_config.mqtt.host, m_config.mqtt.port, QSslConfiguration::defaultConfiguration(), false, this);
	m_mqttClient->setClientId(m_myId);

	connect(m_mqttClient, &QMQTT::Client::connected, this, &P2PClient::onMQTTConnected);
	connect(m_mqttClient, &QMQTT::Client::received, this, &P2PClient::onMQTTReceived);
}

void P2PClient::connectToBroker() {
	qDebug() << "Connecting to broker...";
	m_mqttClient->connectToHost();
}

void P2PClient::onMQTTConnected() {
	qDebug() << "Connected to broker";
	m_mqttClient->subscribe(m_myId, 1);
	emit brokerConnected();
}

void P2PClient::onMQTTReceived(const QMQTT::Message &message) {
	auto doc = QJsonDocument::fromJson(message.payload());
	if (doc.isObject()) handleSignalingMessage(doc.object());
}

void P2PClient::sendSignalingMessage(const QJsonObject &message) {
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

void P2PClient::setupWebRTC() {
	qInfo() << "Initializing WebRTC PeerConnection...";
	emit connectionStateChanged(2, "Initializing WebRTC PeerConnection...");
	Configuration config;

	emit connectionStateChanged(3, "Adding ICE servers");
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
		sendSignalingMessage({
			{"type", QString::fromStdString(description.typeString())},
			{"sdp", QString::fromStdString(std::string(description))}
		});
	});

	m_peerConnection->onLocalCandidate([this](Candidate candidate) {
		qDebug() << "Got local ICE candidate:" << QString::fromStdString(std::string(candidate));
		sendSignalingMessage({
			{"type", "candidate"},
			{"candidate", QString::fromStdString(std::string(candidate))},
			{"mid", QString::fromStdString(candidate.mid())}
		});
	});

	m_peerConnection->onStateChange([this, pc = m_peerConnection.get()](PeerConnection::State state) {
		QMetaObject::invokeMethod(this, [this, state, pc]() {
			if (m_peerConnection.get() != pc) {
				return;
			}
			QString stateStr;
			switch(state) {
				case PeerConnection::State::New:			stateStr = "New";			break;
				case PeerConnection::State::Connecting:
					stateStr = "Connecting";
					emit connectionStateChanged(4, "Establishing P2P connection...");
					break;
				case PeerConnection::State::Connected:		stateStr = "Connected";		break;
				case PeerConnection::State::Disconnected:	stateStr = "Disconnected";	break;
				case PeerConnection::State::Failed:			stateStr = "Failed";		break;
				case PeerConnection::State::Closed:			stateStr = "Closed";		break;
				default:									stateStr = "Unknown";		break;
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
			if (dataChannel->label() == "control") {
				m_controlChannel = dataChannel;
				wireDataChannel(m_controlChannel);
			} else if (dataChannel->label() == "binary") {
				m_binaryChannel = dataChannel;
				wireDataChannel(m_binaryChannel);
			}
		});
	});
}

void P2PClient::handleSignalingMessage(const QJsonObject &msg) {
	QString type = msg["type"].toString();
	qInfo() << "Got incoming message of type -" << type;

	if (type == "offer") {
		emit connectionStateChanged(3, "Incoming connection...");

		closeConnection();
		m_targetId = msg["from"].toString();
		setupWebRTC();

		std::string sdp = msg["sdp"].toString().toStdString();
		qDebug() << "Received remote offer SDP:\n" << QString::fromStdString(sdp);

		m_peerConnection->setRemoteDescription(Description(sdp, type.toStdString()));
		m_peerConnection->setLocalDescription();
	} else if (type == "answer") {
		if (!m_peerConnection) return;
		std::string sdp = msg["sdp"].toString().toStdString();
		qDebug() << "Received remote answer SDP:\n" << QString::fromStdString(sdp);

		m_peerConnection->setRemoteDescription(Description(sdp, type.toStdString()));
	} else if (type == "candidate") {
		if (!m_peerConnection) return;

		std::string sdp = msg["candidate"].toString().toStdString();
		std::string mid = msg["mid"].toString().toStdString();
		qDebug() << "Received remote ICE candidate:" << QString::fromStdString(sdp);

		try {
			m_peerConnection->addRemoteCandidate(Candidate(sdp, mid));
		} catch (const std::exception& e) {
			qWarning() << "Ignoring ICE candidate, remote description not ready yet:" << e.what();
		}
	} else {
		qWarning() << "Unknown signaling message type received:" << type;
	}
}

void P2PClient::checkConnectionReady() {
	if (m_controlChannelOpen && m_binaryChannelOpen) {
		qInfo() << "Both WebRTC DataChannels are open";
		emit connectionStateChanged(5, "Connection established!");
		emit connectionEstablished();
	}
}

void P2PClient::wireDataChannel(std::shared_ptr<rtc::DataChannel> channel) {
	if (!channel) return;

	if (channel->label() == "binary") {
		channel->onBufferedAmountLow([this]() {
			QMetaObject::invokeMethod(this, [this]() {
				emit backpressureStateChanged(false);
			});
		});
	}

	channel->onOpen([this, channel]() {
		QMetaObject::invokeMethod(this, [this, channel]() {
			qInfo() << "WebRTC DataChannel opened successfully:" << QString::fromStdString(channel->label());
			if (channel->label() == "binary") {
				try { channel->setBufferedAmountLowThreshold(2 * 1024 * 1024); } catch(...) {}
				m_binaryChannelOpen = true;
			} else if (channel->label() == "control") {
				m_controlChannelOpen = true;
			}
			checkConnectionReady();
		});
	});

	channel->onClosed([this, channel]() {
		qWarning() << "WebRTC DataChannel closed:" << QString::fromStdString(channel->label());
	});

	channel->onMessage([this, channel](std::variant<binary, std::string> message) {
		if (auto* text = std::get_if<std::string>(&message)) {
			if (channel->label() == "control") {
				QString json = QString::fromStdString(*text);
				qDebug() << "Received JSON command over Control Channel. Length:" << json.length();
				QMetaObject::invokeMethod(this, [this, json]() {
					auto doc = QJsonDocument::fromJson(json.toUtf8());
					if (doc.isObject()) emit jsonReceived(doc.object());
				});
			}
		} else {
			if (channel->label() == "binary") {
				auto& bin = std::get<binary>(message);
				QByteArray chunk(reinterpret_cast<const char *>(bin.data()), bin.size());
				QMetaObject::invokeMethod(this, [this, chunk]() {
					emit binaryReceived(chunk);
				});
			}
		}
	});
}

void P2PClient::call(const QString &targetId) {
	if (targetId == m_myId) {
		qWarning() << "Attempted to call self!";
		return;
	}

	qInfo() << "Initiating call to target:" << targetId;
	emit connectionStateChanged(1, "Connecting to target...");
	closeConnection();
	m_targetId = targetId;
	setupWebRTC();
	m_controlChannel = m_peerConnection->createDataChannel("control");
	m_binaryChannel = m_peerConnection->createDataChannel("binary");
	wireDataChannel(m_controlChannel);
	wireDataChannel(m_binaryChannel);
	m_peerConnection->setLocalDescription();
}

void P2PClient::sendJson(const QJsonObject &json) {
	if (m_controlChannel && m_controlChannel->isOpen()) {
		qDebug() << "Sending JSON over Control Channel:" << json["action"].toString() << json["file_name"].toString();
		m_controlChannel->send(QJsonDocument(json).toJson(QJsonDocument::Compact).toStdString());
	} else {
		qWarning() << "Failed to send JSON: Control Channel is not open!";
	}
}

void P2PClient::sendBinary(const QByteArray& data) {
	if (m_binaryChannel && m_binaryChannel->isOpen()) {
		rtc::binary binChunk(reinterpret_cast<const std::byte*>(data.constData()),
												 reinterpret_cast<const std::byte*>(data.constData()) + data.size());
		m_binaryChannel->send(binChunk);
		if (m_binaryChannel->bufferedAmount() > 16 * 1024 * 1024) {
			emit backpressureStateChanged(true);
		}
	}
}

void P2PClient::closeConnection() {
	qInfo() << "Closing WebRTC connection and DataChannels...";
	if (m_controlChannel) {
		m_controlChannel->close();
		m_controlChannel.reset();
	}
	if (m_binaryChannel) {
		m_binaryChannel->close();
		m_binaryChannel.reset();
	}
	m_controlChannelOpen = false;
	m_binaryChannelOpen = false;

	if (m_peerConnection) {
		m_peerConnection->close();
		m_peerConnection.reset();
	}
}

qint64 P2PClient::getBufferedAmount() const {
	if (m_binaryChannel && m_binaryChannel->isOpen()) {
		return static_cast<qint64>(m_binaryChannel->bufferedAmount());
	}
	return 0;
}