#include "mainwindow.h"
#include "./ui_mainwindow.h"
#include <QClipboard>
#include <QFileDialog>
#include <QMessageBox>
#include <QApplication>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), m_appConfig(AppConfig::load(m_configPath)), ui(std::make_unique<Ui::MainWindow>()) {
	ui->setupUi(this);

	qDebug() << "Loading configuration from:" << m_configPath;

	m_p2pClient = new P2PClient(m_appConfig, this);
	ui->myIdLabel->setText(m_p2pClient->getMyId());

	ui->downloadPath->setText(m_appConfig.downloadPath);

	connect(m_p2pClient, &P2PClient::binaryReceived, this, [this](const QByteArray& data) {
		if (data.size() < 4) return;
		int id;
		memcpy(&id, data.constData(), 4);
		if (m_transfers.contains(id)) m_transfers[id].manager->handleBinaryChunk(data.mid(4));
	});

	connect(m_p2pClient, &P2PClient::jsonReceived, this, [this](const QJsonObject& json) {
		int id = json["transfer_id"].toInt();
		if (!m_transfers.contains(id)) createTransferManager(id);
		if (m_transfers.contains(id)) m_transfers[id].manager->handleJsonCommand(json);
	});

	connect(m_p2pClient, &P2PClient::connectionEstablished, this, [this]() {
		QMessageBox::information(this, "Success!", "Connection established");
		ui->callButton->setEnabled(false);
		ui->sendFileButton->setEnabled(true);
	});

	connect(m_p2pClient, &P2PClient::connectionClosed, this, [this]() {
		for (const auto& session : m_transfers) session.manager->onPeerDisconnected();
		ui->targetIdLineEdit->clear();
		ui->callButton->setEnabled(true);
		ui->sendFileButton->setEnabled(false);
		QMessageBox::warning(this, "Warning!", "Peer disconnected!");
	});

	connect(m_p2pClient, &P2PClient::connectionStateChanged, this, [this](int step, QString status, int maxSteps) {
		ui->statusbar->showMessage(QString("Connection Step %1/%2: %3").arg(step).arg(maxSteps).arg(status), 5000);
	});

	connect(ui->speedLimitSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, &MainWindow::updateSpeedLimits);

	m_p2pClient->connectToBroker();
}

MainWindow::~MainWindow() = default;

FileTransferManager* MainWindow::createTransferManager(int id) {
	auto manager = new FileTransferManager(this);
	manager->setDownloadPath(ui->downloadPath->text());

	auto transferWidget = new QWidget(ui->transfersContainer);
	auto layout = new QVBoxLayout(transferWidget);
	layout->setContentsMargins(0, 0, 0, 10);

	auto nameLabel = new QLabel(transferWidget);
	nameLabel->setAlignment(Qt::AlignCenter);
	layout->addWidget(nameLabel);

	auto progressBar = new QProgressBar(transferWidget);
	progressBar->setAlignment(Qt::AlignCenter);
	progressBar->setValue(0);
	layout->addWidget(progressBar);

	auto hLayout = new QHBoxLayout();
	auto statusLabel = new QLabel(transferWidget);
	auto pauseButton = new QPushButton("Pause", transferWidget);
	pauseButton->setEnabled(false);
	auto cancelButton = new QPushButton("Cancel", transferWidget);
	
	hLayout->addWidget(statusLabel);
	hLayout->addStretch();
	hLayout->addWidget(pauseButton);
	hLayout->addWidget(cancelButton);
	layout->addLayout(hLayout);

	auto line = new QFrame(transferWidget);
	line->setFrameShape(QFrame::HLine);
	line->setFrameShadow(QFrame::Sunken);
	layout->addWidget(line);

	ui->verticalLayout_transfers->addWidget(transferWidget);

	m_transfers[id] = {manager, transferWidget, nameLabel, progressBar, statusLabel, pauseButton, cancelButton};

	connect(pauseButton, &QPushButton::clicked, manager, &FileTransferManager::togglePause);
	connect(cancelButton, &QPushButton::clicked, manager, &FileTransferManager::cancelTransfer);

	connect(manager, &FileTransferManager::sendBinaryData, this, [this, id](const QByteArray& data) {
		QByteArray pkt;
		pkt.append(reinterpret_cast<const char*>(&id), sizeof(id)); 
		pkt.append(data);
		m_p2pClient->sendBinary(pkt);
	});

	connect(manager, &FileTransferManager::sendJsonCommand, this, [this, id](QJsonObject json) {
		json["transfer_id"] = id;
		m_p2pClient->sendJson(json);
	});

	connect(manager, &FileTransferManager::transferStarted, this, [this, id](const QString& name) {
		if (auto it = m_transfers.find(id); it != m_transfers.end()) {
			it->nameLabel->setText(name);
			it->progressBar->setMaximum(100);
			it->progressBar->setValue(0);
			it->pauseButton->setEnabled(true);
			it->pauseButton->setText("Pause");
			it->cancelButton->setEnabled(true);
			ui->selectDownloadPathButton->setEnabled(false);
		}
	});

	connect(manager, &FileTransferManager::progressUpdated, this, [this, id](qint64 cur, qint64 total) {
		if (auto it = m_transfers.find(id); it != m_transfers.end()) {
			it->progressBar->setValue((total > 0) ? static_cast<int>((cur * 100) / total) : 0);
		}
	});

	connect(manager, &FileTransferManager::speedUpdated, this, [this, id](double mbps, int eta) {
		if (auto it = m_transfers.find(id); it != m_transfers.end()) {
			it->statusLabel->setText(QString("Bandwidth: %1 MB/s | ETA: %2 seconds").arg(mbps, 0, 'f', 2).arg(eta));
		}
	});

	connect(manager, &FileTransferManager::transferPaused, this, [this, id](bool paused) {
		if (auto it = m_transfers.find(id); it != m_transfers.end()) {
			it->pauseButton->setText(paused ? "Resume" : "Pause");
			if (paused) it->statusLabel->setText("Paused");
		}
	});

	connect(manager, &FileTransferManager::transferFinished, this, [this, id]() {
		qInfo() << "Transfer finished ID:" << id;
		cleanupTransfer(id);
		if (m_transfers.isEmpty()) {
			QMessageBox::information(this, "Success!", "All file transfers finished successfully");
		}
	});

	connect(manager, &FileTransferManager::transferCanceled, this, [this, id]() {
		qWarning() << "Transfer canceled ID:" << id;
		cleanupTransfer(id);
	});

	connect(manager, &FileTransferManager::hashProgressUpdated, this, [this, id](qint64 current, qint64 total) {
		if (auto it = m_transfers.find(id); it != m_transfers.end()) {
			it->progressBar->setMaximum(100);
			it->progressBar->setValue((total > 0) ? static_cast<int>((current * 100) / total) : 0);
			it->statusLabel->setText("Calculating hash...");
		}
	});

	updateSpeedLimits();
	return manager;
}

void MainWindow::cleanupTransfer(int id) {
	if (auto it = m_transfers.find(id); it != m_transfers.end()) {
		it->container->deleteLater();
		it->manager->deleteLater();
		m_transfers.erase(it);
	}
	ui->selectDownloadPathButton->setEnabled(m_transfers.isEmpty());
	updateSpeedLimits();
}

void MainWindow::updateSpeedLimits() {
	int maxLimit = ui->speedLimitSpinBox->value();
	int limitPerTransfer = maxLimit > 0 ? (maxLimit / qMax(1, (int)m_transfers.size())) : 0;
	for (const auto& session : m_transfers) session.manager->setSpeedLimit(limitPerTransfer);
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
	for (const QString& path : QFileDialog::getOpenFileNames(this)) {
		qInfo() << "Selected file to send:" << path;
		createTransferManager(m_nextTransferId++)->sendFile(path);
	}
}

void MainWindow::on_copyIdButton_clicked() {
	qDebug() << "Local ID copied to clipboard.";
	QGuiApplication::clipboard()->setText(ui->myIdLabel->text());
}

void MainWindow::on_selectDownloadPathButton_clicked() {
	QString dir = QFileDialog::getExistingDirectory(this, "Select download destination...", ui->downloadPath->text(), QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
	if (!dir.isEmpty()) {
		ui->downloadPath->setText(dir);
		for (const auto& session : m_transfers) session.manager->setDownloadPath(dir);

		m_appConfig.downloadPath = dir;
		if (m_appConfig.save(m_configPath)) {
			qInfo() << "Download path saved to config.json:" << dir;
		}
	}
}