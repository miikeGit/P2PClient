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

	ui->downloadPath->setText(m_appConfig.downloadPath);

	connect(m_p2pClient, &P2PClient::binaryReceived, this, [this](const QByteArray& data) {
		if (data.size() < 4) return;
    int id = *reinterpret_cast<const int*>(data.constData());
    if (m_transfers.contains(id)) m_transfers[id]->handleBinaryChunk(data.mid(4));
	});

	connect(m_p2pClient, &P2PClient::jsonReceived, this, [this](const QJsonObject& json) {
		int id = json["transfer_id"].toInt();
		if (!m_transfers.contains(id)) createTransferManager(id);
		if (m_transfers.contains(id)) m_transfers[id]->handleJsonCommand(json);
	});

	connect(m_p2pClient, &P2PClient::connectionEstablished, this, [this]() {
		QMessageBox::information(this, "Success!", "Connection established");
		ui->callButton->setEnabled(false);
		ui->sendFileButton->setEnabled(true);
	});

	connect(m_p2pClient, &P2PClient::connectionClosed, this, [this]() {
		auto managers = m_transfers.values();
		for (auto manager : managers) {
			manager->onPeerDisconnected();
		}
		ui->targetIdLineEdit->clear();
		ui->callButton->setEnabled(true);
		ui->sendFileButton->setEnabled(false);
		QMessageBox::warning(this, "Warning!", "Peer disconnected!");
	});

	connect(m_p2pClient, &P2PClient::connectionStateChanged, this, [this](int step, QString status, int maxSteps) {
		ui->statusbar->showMessage(QString("Connection Step %1/%2: %3").arg(step).arg(maxSteps).arg(status), 5000);
	});

	connect(ui->speedLimitSpinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
		updateSpeedLimits();
	});

	m_p2pClient->connectToBroker();
}

MainWindow::~MainWindow() {}

FileTransferManager* MainWindow::createTransferManager(int id) {
	auto manager = new FileTransferManager(this);
	manager->setDownloadPath(ui->downloadPath->text());

	QWidget* transferWidget = new QWidget(ui->transfersContainer);
	QVBoxLayout* layout = new QVBoxLayout(transferWidget);
	layout->setContentsMargins(0, 0, 0, 10);

	QLabel* nameLabel = new QLabel(transferWidget);
	nameLabel->setAlignment(Qt::AlignCenter);
	layout->addWidget(nameLabel);

	QProgressBar* progressBar = new QProgressBar(transferWidget);
	progressBar->setAlignment(Qt::AlignCenter);
	progressBar->setValue(0);
	layout->addWidget(progressBar);

	QHBoxLayout* hLayout = new QHBoxLayout();
	QLabel* statusLabel = new QLabel(transferWidget);
	QPushButton* cancelButton = new QPushButton("Cancel", transferWidget);
	
	hLayout->addWidget(statusLabel);
	hLayout->addStretch();
	hLayout->addWidget(cancelButton);
	layout->addLayout(hLayout);

	QFrame* line = new QFrame(transferWidget);
	line->setFrameShape(QFrame::HLine);
	line->setFrameShadow(QFrame::Sunken);
	layout->addWidget(line);

	ui->verticalLayout_transfers->addWidget(transferWidget);

	TransferUI tUi;
	tUi.container = transferWidget;
	tUi.nameLabel = nameLabel;
	tUi.progressBar = progressBar;
	tUi.statusLabel = statusLabel;
	tUi.cancelButton = cancelButton;
	m_transferUIs[id] = tUi;

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

	connect(manager, &FileTransferManager::transferStarted, this, [this, id](QString name) {
		if (!m_transferUIs.contains(id)) return;
		auto& tUi = m_transferUIs[id];
		tUi.nameLabel->setText(name);
		tUi.progressBar->setMaximum(100);
		tUi.progressBar->setValue(0);
		tUi.cancelButton->setEnabled(true);
		ui->selectDownloadPathButton->setEnabled(false);
	});

	connect(manager, &FileTransferManager::progressUpdated, this, [this, id](qint64 cur, qint64 total) {
		if (!m_transferUIs.contains(id)) return;
		int percent = (total > 0) ? static_cast<int>((cur * 100) / total) : 0;
		m_transferUIs[id].progressBar->setValue(percent);
	});

	connect(manager, &FileTransferManager::speedUpdated, this, [this, id](double mbps, int eta) {
		if (!m_transferUIs.contains(id)) return;
		m_transferUIs[id].statusLabel->setText(QString("Bandwidth: %1 MB/s | ETA: %2 seconds").arg(mbps, 0, 'f', 2).arg(eta));
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
		if (!m_transferUIs.contains(id)) return;
		m_transferUIs[id].progressBar->setMaximum(100);
		int percent = (total > 0) ? static_cast<int>((current * 100) / total) : 0;
		m_transferUIs[id].progressBar->setValue(percent);
		m_transferUIs[id].statusLabel->setText("Calculating hash...");
	});

	m_transfers[id] = manager;
	updateSpeedLimits();
	return manager;
}

void MainWindow::cleanupTransfer(int id) {
	if (m_transferUIs.contains(id)) {
		m_transferUIs[id].container->deleteLater();
		m_transferUIs.remove(id);
	}
	if (m_transfers.contains(id)) {
		m_transfers[id]->deleteLater();
		m_transfers.remove(id);
	}

	if (m_transfers.isEmpty()) {
		ui->selectDownloadPathButton->setEnabled(true);
	}
	updateSpeedLimits();
}

void MainWindow::updateSpeedLimits() {
	int maxLimit = ui->speedLimitSpinBox->value();
	int limitPerTransfer = maxLimit > 0 ? (maxLimit / qMax(1, (int)m_transfers.size())) : 0;
	auto managers = m_transfers.values();
	for (auto manager : managers) {
		manager->setSpeedLimit(limitPerTransfer);
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
	QStringList paths = QFileDialog::getOpenFileNames(this);

	if (!paths.isEmpty()) {
		for(const QString& path : paths) {
			qInfo() << "Selected file to send:" << path;
			int id = m_nextTransferId++;
			auto manager = createTransferManager(id);
			manager->sendFile(path);
		}
	}
	else {
		qDebug() << "File selection canceled";
	}
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
		
		auto managers = m_transfers.values();
		for(auto manager : managers) {
			manager->setDownloadPath(dir);
		}

		m_appConfig.downloadPath = dir;
		if (m_appConfig.save(m_configPath)) {
			qInfo() << "Download path saved to config.json:" << dir;
		}
	}
}