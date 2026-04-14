#include "mainwindow.h"
#include "./ui_mainwindow.h"
#include <QClipboard>
#include <QFileDialog>
#include <QMessageBox>

#include "appconfig.h"

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), m_appConfig(AppConfig::load(m_configPath)), ui(std::make_unique<Ui::MainWindow>()) {
	ui->setupUi(this);

	qDebug() << "Loading configuration from:" << m_configPath;

	m_p2pClient = new P2PClient(m_appConfig, this);
	ui->myIdLabel->setText(m_p2pClient->getMyId());
	m_fileManager = new FileTransferManager(this);

	ui->downloadPath->setText(m_appConfig.downloadPath);
	m_fileManager->setDownloadPath(m_appConfig.downloadPath);

	connect(m_p2pClient, &P2PClient::binaryReceived, m_fileManager, &FileTransferManager::handleBinaryChunk);
	connect(m_p2pClient, &P2PClient::jsonReceived, m_fileManager, &FileTransferManager::handleJsonCommand);

	connect(m_fileManager, &FileTransferManager::sendBinaryData, m_p2pClient, &P2PClient::sendBinary);
	connect(m_fileManager, &FileTransferManager::sendJsonCommand, m_p2pClient, &P2PClient::sendJson);

	connect(m_p2pClient, &P2PClient::connectionEstablished, this, [this]() {
		QMessageBox::information(this, "Success!", "Connection established");
		ui->callButton->setEnabled(false);
		ui->sendFileButton->setEnabled(true);
	});

	connect(m_p2pClient, &P2PClient::connectionClosed, this, [this]() {
		m_fileManager->onPeerDisconnected();
		ui->targetIdLineEdit->clear();
		ui->callButton->setEnabled(true);
		ui->progressBar->setValue(0);
		ui->sendFileButton->setEnabled(false);
		QMessageBox::warning(this, "Warning!", "Peer disconnected!");
	});

	connect(m_fileManager, &FileTransferManager::transferStarted, this, [this](QString name) {
		qInfo() << "File transfer started";
		ui->fileNameLabel->setText(name);
		ui->progressBar->setMaximum(100);
		ui->progressBar->setValue(0);
		ui->cancelButton->setEnabled(true);
		ui->selectDownloadPathButton->setEnabled(false);
	});

	connect(m_fileManager, &FileTransferManager::progressUpdated, this, [this](qint64 cur, qint64 total) {
		int percent = (total > 0) ? static_cast<int>((cur * 100) / total) : 0;
		ui->progressBar->setValue(percent);
	});

	connect(m_fileManager, &FileTransferManager::speedUpdated, this, [this](double mbps, int eta) {
		ui->statusLabel->setText(QString("Bandwidth: %1 MB/s | ETA: %2 seconds").arg(mbps, 0, 'f', 2).arg(eta));
	});

	connect(m_fileManager, &FileTransferManager::transferFinished, this, [this]() {
		qInfo() << "Transfer finished";
		QMessageBox::information(this, "Success!", "File transfer finished successfully");
		ClearFileInfo();
	});

	connect(m_fileManager, &FileTransferManager::transferCanceled, this, [this]() {
		qWarning() << "Transfer canceled";
		ClearFileInfo();
	});

	connect(m_p2pClient, &P2PClient::connectionStateChanged, this, [this](int step, QString status, int maxSteps) {
		ui->progressBar->setMaximum(maxSteps);
		ui->progressBar->setValue(step);
		ui->statusLabel->setText(status);
	});

	connect(m_fileManager, &FileTransferManager::hashProgressUpdated, this, [this](qint64 current, qint64 total) {
		ui->progressBar->setMaximum(100);
		int percent = (total > 0) ? static_cast<int>((current * 100) / total) : 0;
		ui->progressBar->setValue(percent);
		ui->statusLabel->setText("Calculating hash...");
	});

	m_p2pClient->connectToBroker();
}

MainWindow::~MainWindow() {}

void MainWindow::on_callButton_clicked() {
	QString target = ui->targetIdLineEdit->text().trimmed();
	if (!target.isEmpty()) {
		qInfo() << "Initiated call to ID:" << target;
		m_p2pClient->call(target);
	} else {
		qWarning() << "Call button clicked, target ID is empty!";
		QMessageBox::warning(this, "Error", "No target ID provided!");
	}
}

void MainWindow::on_sendFileButton_clicked() {
	qDebug() << "Opening File Dialog...";
	QString path = QFileDialog::getOpenFileName(this);

	if (!path.isEmpty()) {
		qInfo() << "Selected file to send:" << path;
		ui->cancelButton->setEnabled(true);
		ui->statusLabel->setText("Calculating hash...");
		m_fileManager->sendFile(path);
	}
	else {
		qDebug() << "File selection canceled";
	}
}

void MainWindow::on_cancelButton_clicked() {
	qWarning() << "Cancel transfer button clicked";
	m_fileManager->cancelTransfer();
}

void MainWindow::on_copyIdButton_clicked() {
	qDebug() << "Local ID copied to clipboard.";
	QGuiApplication::clipboard()->setText(ui->myIdLabel->text());
}

void MainWindow::on_selectDownloadPathButton_clicked() {
	QString currentPath = ui->downloadPath->text();
	QString dir = QFileDialog::getExistingDirectory(this, "Select download destination...", currentPath,
																									QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
	if (!dir.isEmpty()) {
		ui->downloadPath->setText(dir);
		m_fileManager->setDownloadPath(dir);

		m_appConfig.downloadPath = dir;
		if (m_appConfig.save(m_configPath)) {
			qInfo() << "Download path saved to config.json:" << dir;
		}
	}
}

void MainWindow::ClearFileInfo() {
	ui->progressBar->setValue(0);
	ui->fileNameLabel->clear();
	ui->cancelButton->setEnabled(false);
	ui->statusLabel->setText("Ready");
	ui->selectDownloadPathButton->setEnabled(true);
}