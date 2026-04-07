#include "mainwindow.h"
#include "./ui_mainwindow.h"
#include <QSslConfiguration>
#include <QUuid>
#include <QDebug>

using namespace rtc;

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), ui(std::make_unique<Ui::MainWindow>()) {
	ui->setupUi(this);
	m_myId = QUuid::createUuid().toString(QUuid::WithoutBraces);
	qDebug() << m_myId;

	SetupMQTT();
	m_mqttClient->connectToHost();
}

MainWindow::~MainWindow() {
	if (m_dataChannel) m_dataChannel->close();
	if (m_peerConnection) m_peerConnection->close();
	m_mqttClient->disconnectFromHost();
}

void MainWindow::SetupMQTT() {
	const QString hostName{"f15e0b8bb05c484bba3e1e7a82c464c3.s1.eu.hivemq.cloud"};
	const quint16 port{8883};
	const QString username{"throwaway"};
	const QByteArray password{"Throwaway1"};

	m_mqttClient = new QMQTT::Client(hostName, port, QSslConfiguration::defaultConfiguration(), false, this);
	m_mqttClient->setUsername(username);
	m_mqttClient->setPassword(password);
	m_mqttClient->setClientId(m_myId);

	connect(m_mqttClient, &QMQTT::Client::connected, this, &MainWindow::onMQTTConnected);
	connect(m_mqttClient, &QMQTT::Client::received, this, &MainWindow::onMQTTReceived);
}

void MainWindow::onMQTTConnected() {
	qDebug() << "Connected";
	m_mqttClient->subscribe(m_myId, 1);
}

void MainWindow::onMQTTReceived(const QMQTT::Message& message) {
	QJsonDocument doc = QJsonDocument::fromJson(message.payload());
	if (!doc.isNull() && doc.isObject()) {
		handleSignalingMessage(doc.object());
	}
}

void MainWindow::SendSignalingMessage(const QJsonObject& message) {
	if (m_targetId.isEmpty()) return;

	QJsonObject msgCopy = message;
	msgCopy["from"] = m_myId;

	QByteArray payload = QJsonDocument(msgCopy).toJson(QJsonDocument::Compact);

	QMetaObject::invokeMethod(this, [this, payload]() {
		QMQTT::Message mqttMsg(1, m_targetId, payload);
		m_mqttClient->publish(mqttMsg);
	});
}

void MainWindow::SetupWebRTC() {
	Configuration config;
	config.iceServers.emplace_back("stun:stun.l.google.com:19302"); // Free STUN server provided by Google

	m_peerConnection = std::make_shared<PeerConnection>(config);

	m_peerConnection->onLocalDescription([this](Description description) {
		QJsonObject msg;
		msg["type"] = QString::fromStdString(description.typeString()); // Offer/Answer
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

	m_peerConnection->onDataChannel([this](std::shared_ptr<DataChannel> dataChannel) {
		QMetaObject::invokeMethod(this, [this, dataChannel]() {
			m_dataChannel = dataChannel;
			wireDataChannel();
		});
	});
}

void MainWindow::handleSignalingMessage(const QJsonObject& msg) {
	QString type = msg["type"].toString();

	if (!m_peerConnection) {
		SetupWebRTC();
	}

	if (type == "offer") {
		m_targetId = msg["from"].toString();
		qDebug() << "Generating answer...";
		std::string sdp = msg["sdp"].toString().toStdString();

		m_peerConnection->setRemoteDescription(Description(sdp, type.toStdString()));
		m_peerConnection->setLocalDescription();

	} else if (type == "answer") {
		qDebug() << "Answered";
		std::string sdp = msg["sdp"].toString().toStdString();

		m_peerConnection->setRemoteDescription(Description(sdp, type.toStdString()));

	} else if (type == "candidate") {
		std::string sdp = msg["candidate"].toString().toStdString();
		std::string mid = msg["mid"].toString().toStdString();
		m_peerConnection->addRemoteCandidate(Candidate(sdp, mid));
	}
}

void MainWindow::wireDataChannel() {
	m_dataChannel->onOpen([this]() {
		QMetaObject::invokeMethod(this, [this]() {
			qDebug() << "DataChannel opened";
		});
	});

	m_dataChannel->onMessage([this](std::variant<binary, std::string> message) {
		if (std::holds_alternative<std::string>(message)) {
			QString text = QString::fromStdString(std::get<std::string>(message));
			QMetaObject::invokeMethod(this, [this, text]() {
				qDebug() << "In (P2P): " + text;
			});
		}
	});

	m_dataChannel->onClosed([this]() {
		QMetaObject::invokeMethod(this, [this]() {
			qDebug() << "DataChannel closed";
		});
	});
}

void MainWindow::on_callButton_clicked() {
	m_targetId = ui->targetIdLineEdit->text().trimmed();
	SetupWebRTC();
	m_dataChannel = m_peerConnection->createDataChannel("");
	wireDataChannel();
	m_peerConnection->setLocalDescription();
}

void MainWindow::on_sendButton_clicked() {
	if (m_dataChannel && m_dataChannel->isOpen()) {
		m_dataChannel->send(ui->messageLineEdit->text().toStdString());
		qDebug() << "P2P: " + ui->messageLineEdit->text();
		ui->messageLineEdit->clear();
	}
}