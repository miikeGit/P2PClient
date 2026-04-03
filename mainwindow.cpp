#include "mainwindow.h"
#include "./ui_mainwindow.h"
#include <QSslConfiguration>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), ui(std::make_unique<Ui::MainWindow>()) {
	ui->setupUi(this);
	SetupMQTT();
}

MainWindow::~MainWindow() {}

void MainWindow::SetupMQTT() {
	const QString hostName{"f15e0b8bb05c484bba3e1e7a82c464c3.s1.eu.hivemq.cloud"};
	const quint16 port{8883};
	const QString username{"throwaway"};
	const QByteArray password{"Throwaway1"};

	m_mqttClient = new QMQTT::Client(hostName, port, QSslConfiguration::defaultConfiguration(), false, this);
	m_mqttClient->setUsername(username);
	m_mqttClient->setPassword(password);

	connect(m_mqttClient, &QMQTT::Client::connected, this, &MainWindow::onMQTTConnected);
	connect(m_mqttClient, &QMQTT::Client::received, this, &MainWindow::onMQTTReceived);
}

void MainWindow::on_connectButton_clicked() {
	ui->logTextEdit->append("Connecting to broker...");
	m_mqttClient->connectToHost();
}


void MainWindow::on_sendButton_clicked() {
	QMQTT::Message msg(1, m_topic, ui->messageLineEdit->text().toUtf8());
	m_mqttClient->publish(msg);
}

void MainWindow::onMQTTConnected() {
	ui->logTextEdit->append("Connected!");
	m_mqttClient->subscribe(m_topic, 1);
}

void MainWindow::onMQTTReceived(const QMQTT::Message& message) {
	ui->logTextEdit->append(QString::fromUtf8(message.payload()));
}