#include "mainwindow.h"
#include "./ui_mainwindow.h"
#include <QSslConfiguration>
#include <QUuid>
#include <QTimer>
#include <QStandardPaths>
#include <QClipboard>

using namespace rtc;

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), ui(std::make_unique<Ui::MainWindow>()) {
	ui->setupUi(this);
	m_myId = QUuid::createUuid().toString(QUuid::WithoutBraces);
	ui->myIdLabel->setText(m_myId);

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

	m_peerConnection->onStateChange([this](PeerConnection::State state) {
		QMetaObject::invokeMethod(this, [this, state]() {
			if (state == PeerConnection::State::Disconnected || state == PeerConnection::State::Failed || state == PeerConnection::State::Closed) {

				if (m_isTransferring && m_fileSenderTimer) {
					m_fileSenderTimer->stop();
					m_fileSenderTimer->deleteLater();
					m_fileSenderTimer = nullptr;
					m_isTransferring = false;
				}

				if (m_incomingFile.isOpen()) {
					m_incomingFile.close();
					m_incomingFile.remove();
				}

				ui->progressBar->setValue(0);
				m_targetId.clear();
				if (m_dataChannel) m_dataChannel->close();
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

void MainWindow::handleSignalingMessage(const QJsonObject& msg) {
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

void MainWindow::wireDataChannel() {
	m_dataChannel->onMessage([this](std::variant<binary, std::string> message) {
		if (std::holds_alternative<std::string>(message)) {
			QString text = QString::fromStdString(std::get<std::string>(message));

			QMetaObject::invokeMethod(this, [this, text]() {
				QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8());
				QJsonObject obj = doc.object();

				if (doc.isNull() || !doc.isObject()) return;

				if (obj.contains("file_name")) {
					m_expectedFileSize = obj["file_size"].toVariant().toLongLong();
					m_receivedBytes = 0;
					m_incomingFile.setFileName(QStandardPaths::writableLocation(QStandardPaths::DownloadLocation) + "/" + obj["file_name"].toString());
					if (!m_incomingFile.open(QIODevice::WriteOnly)) return;

					ui->fileNameLabel->setText(obj["file_name"].toString());
					ui->progressBar->setMaximum(m_expectedFileSize);
					ui->progressBar->setValue(0);
					m_lastSpeedCheckBytes = 0;
					m_transferSpeedTimer.start();
				}

				if (obj.contains("action")) {
					QString action = obj["action"].toString();

					if (action == "cancel_transfer") {
						if (m_incomingFile.isOpen()) {
							m_incomingFile.close();
							m_incomingFile.remove();
							ui->progressBar->setValue(0);
						}
					}
					else if (action == "receiver_canceled") {
						if (m_isTransferring && m_fileSenderTimer) {
							m_fileSenderTimer->stop();
							m_fileSenderTimer->deleteLater();
							m_fileSenderTimer = nullptr;
							m_isTransferring = false;
							ui->progressBar->setValue(0);
						}
					}
					return;
				}
			});
		}

		else if (std::holds_alternative<binary>(message)) {
			auto binChunk = std::get<binary>(message);
			QByteArray chunk(reinterpret_cast<const char*>(binChunk.data()), binChunk.size());

			QMetaObject::invokeMethod(this, [this, chunk]() {
				if (m_incomingFile.isOpen()) {
					m_incomingFile.write(chunk);
					m_receivedBytes += chunk.size();
					ui->progressBar->setValue(m_receivedBytes);
					qint64 elapsedMs = m_transferSpeedTimer.elapsed();

					if (elapsedMs > 1000) {
						double bytesPerSec = ((m_receivedBytes - m_lastSpeedCheckBytes) * 1000.0) / elapsedMs;
						double MBps = bytesPerSec / (1024.0 * 1024.0);

						qint64 remaining = m_expectedFileSize - m_receivedBytes;
						double eta = remaining / bytesPerSec;

						QString status = QString("Speed: %1 MB/s | ETA: %2s").arg(MBps, 0, 'f', 2).arg((int)eta);

						ui->transferSpeed->setText(status);

						m_transferSpeedTimer.restart();
						m_lastSpeedCheckBytes = m_receivedBytes;
					}

					if (m_receivedBytes >= m_expectedFileSize) {
						m_incomingFile.close();
						ui->progressBar->setValue(0);
					}
				}
			});
		}
	});
}

void MainWindow::on_callButton_clicked() {
	if (m_peerConnection) {
		if (m_dataChannel) m_dataChannel->close();
		m_peerConnection->close();
	}

	m_targetId = ui->targetIdLineEdit->text().trimmed();
	SetupWebRTC();
	m_dataChannel = m_peerConnection->createDataChannel("");
	wireDataChannel();
	m_peerConnection->setLocalDescription();
}

void MainWindow::on_sendFileButton_clicked() {
	QString filePath = QFileDialog::getOpenFileName(this);
	if (filePath.isEmpty()) return;

	auto file = std::make_shared<QFile>(filePath);
	if (!file->open(QIODevice::ReadOnly)) return;

	QFileInfo fileInfo(*file);
	QJsonObject meta;
	meta["file_name"] = fileInfo.fileName();
	meta["file_size"] = fileInfo.size();
	m_dataChannel->send(QJsonDocument(meta).toJson(QJsonDocument::Compact).toStdString());

	m_lastSpeedCheckBytes = 0;
	ui->fileNameLabel->setText(fileInfo.fileName());
	ui->progressBar->setMaximum(fileInfo.size());
	ui->progressBar->setValue(0);
	m_isTransferring = true;
	m_transferSpeedTimer.start();

	m_fileSenderTimer = new QTimer(this);
	connect(m_fileSenderTimer, &QTimer::timeout, this, [this, file]() {
		if (file->atEnd() || !m_dataChannel->isOpen()) {
			file->close();
			m_fileSenderTimer->deleteLater();
			ui->progressBar->setValue(0);
			m_isTransferring = false;
			return;
		}
		QByteArray chunk = file->read(CHUNK_SIZE);
		binary binChunk(
			reinterpret_cast<const std::byte*>(chunk.constData()),
			reinterpret_cast<const std::byte*>(chunk.constData()) + chunk.size());
		m_dataChannel->send(std::move(binChunk));
		ui->progressBar->setValue(file->pos());

		qint64 elapsedMs = m_transferSpeedTimer.elapsed();
		if (elapsedMs > 1000) {
			qint64 bytesSent = file->pos();
			double bytesPerSec = ((bytesSent - m_lastSpeedCheckBytes) * 1000.0) / elapsedMs;
			double MBps = bytesPerSec / (1024.0 * 1024.0);

			qint64 remaining = file->size() - file->pos();
			double eta = remaining / bytesPerSec;

			QString status = QString("Speed: %1 MB/s | ETA: %2s").arg(MBps, 0, 'f', 2).arg((int)eta);

			ui->transferSpeed->setText(status);
			ui->progressBar->setValue(bytesSent);
			m_transferSpeedTimer.restart();
			m_lastSpeedCheckBytes = bytesSent;
		}
	});
	m_fileSenderTimer->start(0);
}

void MainWindow::on_cancelButton_clicked() {
	if (!m_dataChannel || !m_dataChannel->isOpen()) return;

	// sender
	if (m_isTransferring) {
		if (m_fileSenderTimer) {
			m_fileSenderTimer->stop();
			m_fileSenderTimer->deleteLater();
			m_fileSenderTimer = nullptr;
		}

		QJsonObject cancelMsg;
		cancelMsg["action"] = "cancel_transfer";
		m_dataChannel->send(QJsonDocument(cancelMsg).toJson(QJsonDocument::Compact).toStdString());

		m_isTransferring = false;
		ui->progressBar->setValue(0);
		return;
	}

	// receiver
	if (m_incomingFile.isOpen()) {
		m_incomingFile.close();
		m_incomingFile.remove();

		QJsonObject cancelMsg;
		cancelMsg["action"] = "receiver_canceled";
		m_dataChannel->send(QJsonDocument(cancelMsg).toJson(QJsonDocument::Compact).toStdString());

		ui->progressBar->setValue(0);
		return;
	}
}

void MainWindow::on_copyIdButton_clicked() {
	QClipboard *clipboard = QGuiApplication::clipboard();
	clipboard->setText(m_myId);
}