#include "mainwindow.h"
#include "./ui_mainwindow.h"
#include <QUuid>
#include <QClipboard>
#include <QFileDialog>

#include "appconfig.h"

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), ui(std::make_unique<Ui::MainWindow>()) {
	ui->setupUi(this);

	m_myId = QUuid::createUuid().toString(QUuid::WithoutBraces);
	ui->myIdLabel->setText(m_myId);

	QString configPath = qApp->applicationDirPath() + "/config.json";
	m_p2pClient = new P2PClient(m_myId, AppConfig::load(configPath), this);
	m_fileManager = new FileTransferManager(this);

	connect(m_p2pClient, &P2PClient::binaryReceived, m_fileManager, &FileTransferManager::handleBinaryChunk);
	connect(m_p2pClient, &P2PClient::jsonReceived, m_fileManager, &FileTransferManager::handleJsonCommand);

	connect(m_fileManager, &FileTransferManager::sendBinaryData, m_p2pClient, &P2PClient::sendBinary);
	connect(m_fileManager, &FileTransferManager::sendJsonCommand, m_p2pClient, &P2PClient::sendJson);

	connect(m_p2pClient, &P2PClient::connectionClosed, m_fileManager, &FileTransferManager::onPeerDisconnected);

	connect(m_fileManager, &FileTransferManager::transferStarted, this, [this](QString name, qint64 size) {
		ui->fileNameLabel->setText(name);
		ui->progressBar->setMaximum(size);
		ui->progressBar->setValue(0);
	});

	connect(m_fileManager, &FileTransferManager::progressUpdated, this, [this](qint64 cur, qint64 total) {
		ui->progressBar->setValue(cur);
	});

	connect(m_fileManager, &FileTransferManager::speedUpdated, this, [this](double mbps, int eta) {
		ui->transferSpeed->setText(QString("Bandwidth: %1 MB/s | ETA: %2 seconds").arg(mbps, 0, 'f', 2).arg(eta));
	});

	connect(m_fileManager, &FileTransferManager::transferFinished, this, [this]() {
		ui->progressBar->setValue(0);
	});

	connect(m_fileManager, &FileTransferManager::transferCanceled, this, [this]() {
		ui->progressBar->setValue(0);
	});

	m_p2pClient->connectToBroker();
}

MainWindow::~MainWindow() {}

void MainWindow::on_callButton_clicked() {
	QString target = ui->targetIdLineEdit->text().trimmed();
	if (!target.isEmpty()) {
		m_p2pClient->call(target);
	}
}

void MainWindow::on_sendFileButton_clicked() {
	QString path = QFileDialog::getOpenFileName(this);
	if (!path.isEmpty()) {
		m_fileManager->sendFile(path);
	}
}

void MainWindow::on_cancelButton_clicked() {
	m_fileManager->cancelTransfer();
}

void MainWindow::on_copyIdButton_clicked() {
	QGuiApplication::clipboard()->setText(m_myId);
}