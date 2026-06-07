#include "mainwindow.h"
#include "./ui_mainwindow.h"
#include <QClipboard>
#include <QFileDialog>
#include <QMessageBox>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), m_appConfig(AppConfig::load(m_configPath)), ui(std::make_unique<Ui::MainWindow>()) {
	ui->setupUi(this);

	qDebug() << "Loading configuration from:" << m_configPath;

	m_p2pClient = new P2PClient(m_appConfig, this);
	ui->myIdLabel->setText(m_p2pClient->getMyId());
	m_fileManager = new FileTransferManager();
	m_workerThread = new QThread(this);
	m_fileManager->moveToThread(m_workerThread);
	connect(m_workerThread, &QThread::finished, m_fileManager, &QObject::deleteLater);

	m_fileManager->setDownloadPath(m_appConfig.downloadPath);
	ui->downloadPath->setText(m_appConfig.downloadPath);
	m_fileManager->setNetworkBufferCallback([this]() -> qint64 {
		return m_p2pClient ? m_p2pClient->getBufferedAmount() : 0;
	});

	connect(m_p2pClient, &P2PClient::binaryReceived, m_fileManager, &FileTransferManager::handleBinaryChunk);
	connect(m_p2pClient, &P2PClient::jsonReceived, m_fileManager, &FileTransferManager::handleJsonCommand);
	connect(m_p2pClient, &P2PClient::backpressureStateChanged, m_fileManager, &FileTransferManager::setBackpressure);
	connect(m_fileManager, &FileTransferManager::sendBinaryData, m_p2pClient, &P2PClient::sendBinary, Qt::DirectConnection);
	connect(m_fileManager, &FileTransferManager::sendJsonCommand, m_p2pClient, &P2PClient::sendJson, Qt::DirectConnection);

	connect(m_p2pClient, &P2PClient::connectionEstablished, this, [this]() {
		ui->callButton->setEnabled(false);
		ui->sendFileButton->setEnabled(true);
	});

	connect(m_p2pClient, &P2PClient::connectionClosed, this, [this]() {
		QMetaObject::invokeMethod(m_fileManager, [mgr = m_fileManager]() { mgr->onPeerDisconnected(); });
		ui->targetIdLineEdit->clear();
		ui->callButton->setEnabled(true);
		ui->sendFileButton->setEnabled(false);
		QMessageBox::warning(this, "Warning!", "Peer disconnected!");
	});

	connect(m_p2pClient, &P2PClient::connectionStateChanged, this, [this](int step, QString status, int maxSteps) {
		ui->statusbar->showMessage(QString("Connection Step %1/%2: %3").arg(step).arg(maxSteps).arg(status), 5000);
	});

	connect(m_fileManager, &FileTransferManager::transferStarted, this, [this](const QString& name) {
		qInfo() << "File transfer started";
		ui->fileNameLabel->setText(name);
		ui->progressBar->setMaximum(100);
		ui->progressBar->setValue(0);
		ui->cancelButton->setEnabled(true);
		ui->selectDownloadPathButton->setEnabled(false);
	});

	connect(m_fileManager, &FileTransferManager::progressUpdated, this, [this](qint64 cur, qint64 total) {
		ui->progressBar->setValue((total > 0) ? static_cast<int>((cur * 100) / total) : 0);
	});

	connect(m_fileManager, &FileTransferManager::speedUpdated, this, [this](double mbps, int eta) {
		ui->statusLabel->setText(QString("Bandwidth: %1 MB/s | ETA: %2 seconds").arg(mbps, 0, 'f', 2).arg(eta));
	});

	connect(m_fileManager, &FileTransferManager::transferFinished, this, [this]() {
		qInfo() << "Transfer finished";
		QMessageBox::information(this, "Success!", "File transfer finished successfully");
		clearFileInfo();
	});

	connect(m_fileManager, &FileTransferManager::transferCanceled, this, [this]() {
		qWarning() << "Transfer canceled";
		clearFileInfo();
	});

	m_workerThread->start();
	m_p2pClient->connectToBroker();
}

MainWindow::~MainWindow() {
	if (m_workerThread) {
		m_workerThread->quit();
		m_workerThread->wait();
	}
}

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
	const QString path = QFileDialog::getOpenFileName(this);
	if (!path.isEmpty()) {
		qInfo() << "Selected file to send:" << path;
		ui->cancelButton->setEnabled(true);
		ui->statusLabel->setText("Preparing...");
		QMetaObject::invokeMethod(m_fileManager, [mgr = m_fileManager, path]() { mgr->sendFile(path); });
	} else {
		qDebug() << "File selection canceled";
	}
}

void MainWindow::on_cancelButton_clicked() {
	qWarning() << "Cancel transfer button clicked";
	QMetaObject::invokeMethod(m_fileManager, [mgr = m_fileManager]() { mgr->cancelTransfer(); });
}

void MainWindow::on_copyIdButton_clicked() {
	qDebug() << "Local ID copied to clipboard.";
	QGuiApplication::clipboard()->setText(ui->myIdLabel->text());
}

void MainWindow::on_selectDownloadPathButton_clicked() {
	QString dir = QFileDialog::getExistingDirectory(this, "Select download destination...", ui->downloadPath->text(), QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
	if (!dir.isEmpty()) {
		ui->downloadPath->setText(dir);
		QMetaObject::invokeMethod(m_fileManager, [mgr = m_fileManager, dir]() { mgr->setDownloadPath(dir); });

		m_appConfig.downloadPath = dir;
		if (m_appConfig.save(m_configPath)) {
			qInfo() << "Download path saved to config.json:" << dir;
		}
	}
}

void MainWindow::clearFileInfo() {
	ui->progressBar->setValue(0);
	ui->fileNameLabel->clear();
	ui->cancelButton->setEnabled(false);
	ui->statusLabel->clear();
	ui->selectDownloadPathButton->setEnabled(true);
}
