#include "mainwindow.h"
#include "./ui_mainwindow.h"
#include <QClipboard>
#include <QFileDialog>
#include <QMessageBox>
#include <QStyle>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDragLeaveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>
#include <QFileInfo>
#include <QLabel>
#include <QFrame>
#include <QVBoxLayout>
#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>
#include <QResizeEvent>

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

	wireSignals();
	buildDropOverlay();
	setAcceptDrops(true);
	ui->targetIdLineEdit->setAcceptDrops(false);
	ui->downloadPath->setAcceptDrops(false);
	setConnectionStatus(false);

	m_workerThread->start();
	m_p2pClient->connectToBroker();
}

MainWindow::~MainWindow() {
	if (m_workerThread) {
		m_workerThread->quit();
		m_workerThread->wait();
	}
}

void MainWindow::wireSignals() {
	connect(m_p2pClient, &P2PClient::binaryReceived, m_fileManager, &FileTransferManager::handleBinaryChunk);
	connect(m_p2pClient, &P2PClient::jsonReceived, m_fileManager, &FileTransferManager::handleJsonCommand);
	connect(m_p2pClient, &P2PClient::backpressureStateChanged, m_fileManager, &FileTransferManager::setBackpressure);
	connect(m_fileManager, &FileTransferManager::sendBinaryData, m_p2pClient, &P2PClient::sendBinary, Qt::DirectConnection);
	connect(m_fileManager, &FileTransferManager::sendJsonCommand, m_p2pClient, &P2PClient::sendJson, Qt::DirectConnection);

	connect(m_p2pClient, &P2PClient::connectionEstablished, this, [this]() {
		setConnectionStatus(true);
		ui->callButton->setEnabled(false);
		ui->sendFileButton->setEnabled(true);
	});

	connect(m_p2pClient, &P2PClient::connectionClosed, this, [this]() {
		setConnectionStatus(false);
		QMetaObject::invokeMethod(m_fileManager, [mgr = m_fileManager]() { mgr->onPeerDisconnected(); });
		ui->targetIdLineEdit->clear();
		ui->callButton->setEnabled(true);
		ui->sendFileButton->setEnabled(false);
		QMessageBox::warning(this, "Warning!", "Peer disconnected!");
	});

	connect(m_p2pClient, &P2PClient::statusChanged, this, [this](const QString& status) {
		ui->statusbar->showMessage(status, 5000);
	});

	connect(m_fileManager, &FileTransferManager::transferStarted, this, [this](const QString& name) {
		qInfo() << "File transfer started";
		m_transferActive = true;
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

void MainWindow::setConnectionStatus(bool online) {
	m_connected = online;
	const QString dot = QString::fromUtf8("\xE2\x97\x8F");
	ui->statusPill->setText(dot + (online ? "  Connected" : "  Offline"));
	ui->statusPill->setProperty("online", online);
	ui->statusPill->style()->unpolish(ui->statusPill);
	ui->statusPill->style()->polish(ui->statusPill);
}

void MainWindow::clearFileInfo() {
	m_transferActive = false;
	ui->progressBar->setValue(0);
	ui->fileNameLabel->clear();
	ui->cancelButton->setEnabled(false);
	ui->statusLabel->clear();
	ui->selectDownloadPathButton->setEnabled(true);
}

void MainWindow::buildDropOverlay() {
	m_dropOverlay = new QWidget(this);
	m_dropOverlay->setObjectName("dropOverlay");
	m_dropOverlay->setAttribute(Qt::WA_TransparentForMouseEvents, true);
	m_dropOverlay->setStyleSheet(R"(
		QWidget#dropOverlay {
		    background-color: rgba(18, 19, 25, 0.88);
		}
		QFrame#dropZone {
		    background-color: rgba(91, 140, 255, 0.06);
		    border: 2px dashed #5b8cff;
		    border-radius: 18px;
		}
		QFrame#dropZone[ready="false"] {
		    background-color: rgba(255, 107, 107, 0.05);
		    border: 2px dashed #ff6b6b;
		}
		QLabel#dropIcon { font-family: "Segoe UI Symbol"; font-size: 46px; color: #5b8cff; }
		QLabel#dropTitle { color: #ffffff; font-size: 20px; font-weight: 700; }
		QLabel#dropSubtitle { color: #8b8f9e; font-size: 13px; }
		QFrame#dropZone[ready="false"] QLabel#dropIcon { color: #ff6b6b; }
	)");

	auto* outer = new QVBoxLayout(m_dropOverlay);
	outer->setContentsMargins(30, 30, 30, 30);

	auto* zone = new QFrame(m_dropOverlay);
	zone->setObjectName("dropZone");
	zone->setProperty("ready", true);

	auto* inner = new QVBoxLayout(zone);
	inner->setSpacing(10);

	m_dropIcon = new QLabel(QString::fromUtf8("\xE2\xAC\x87"), zone);
	m_dropIcon->setObjectName("dropIcon");
	m_dropIcon->setAlignment(Qt::AlignCenter);

	m_dropTitle = new QLabel("Drop file here", zone);
	m_dropTitle->setObjectName("dropTitle");
	m_dropTitle->setAlignment(Qt::AlignCenter);

	m_dropSubtitle = new QLabel("Release to send it to your peer", zone);
	m_dropSubtitle->setObjectName("dropSubtitle");
	m_dropSubtitle->setAlignment(Qt::AlignCenter);

	inner->addStretch();
	inner->addWidget(m_dropIcon);
	inner->addWidget(m_dropTitle);
	inner->addWidget(m_dropSubtitle);
	inner->addStretch();
	outer->addWidget(zone);

	m_dropOverlayOpacity = new QGraphicsOpacityEffect(m_dropOverlay);
	m_dropOverlayOpacity->setOpacity(0.0);
	m_dropOverlay->setGraphicsEffect(m_dropOverlayOpacity);

	m_dropOverlayAnim = new QPropertyAnimation(m_dropOverlayOpacity, "opacity", this);
	m_dropOverlayAnim->setDuration(140);
	connect(m_dropOverlayAnim, &QPropertyAnimation::finished, this, [this]() {
		if (m_dropOverlayOpacity->opacity() <= 0.01) m_dropOverlay->hide();
	});

	m_dropOverlay->setGeometry(rect());
	m_dropOverlay->hide();
}

void MainWindow::setDropOverlayVisible(bool visible) {
	if (!m_dropOverlay) return;
	if (visible) {
		m_dropOverlay->setGeometry(rect());
		m_dropOverlay->raise();
		m_dropOverlay->show();
	}
	m_dropOverlayAnim->stop();
	m_dropOverlayAnim->setStartValue(m_dropOverlayOpacity->opacity());
	m_dropOverlayAnim->setEndValue(visible ? 1.0 : 0.0);
	m_dropOverlayAnim->start();
}

void MainWindow::updateDropOverlayState(bool ready) {
	if (!m_dropOverlay) return;
	if (QFrame* zone = m_dropOverlay->findChild<QFrame*>("dropZone")) {
		zone->setProperty("ready", ready);
		zone->style()->unpolish(zone);
		zone->style()->polish(zone);
	}
	if (ready) {
		m_dropIcon->setText(QString::fromUtf8("\xE2\xAC\x87"));
		m_dropTitle->setText("Drop file here");
		m_dropSubtitle->setText("Release to send it to your peer");
	} else if (!m_connected) {
		m_dropIcon->setText(QString::fromUtf8("\xE2\x9C\x95"));
		m_dropTitle->setText("Not connected");
		m_dropSubtitle->setText("Connect to a peer before sending files");
	} else {
		m_dropIcon->setText(QString::fromUtf8("\xE2\x8F\xB3"));
		m_dropTitle->setText("Transfer in progress");
		m_dropSubtitle->setText("Wait for the current transfer to finish");
	}
}

QString MainWindow::firstLocalFile(const QMimeData *mime) const {
	if (!mime || !mime->hasUrls()) return QString();
	const QList<QUrl> urls = mime->urls();
	for (const QUrl& url : urls) {
		if (!url.isLocalFile()) continue;
		const QString local = url.toLocalFile();
		QFileInfo info(local);
		if (info.exists() && info.isFile()) return local;
	}
	return QString();
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event) {
	if (firstLocalFile(event->mimeData()).isEmpty()) return;
	const bool ready = isReadyToSend();
	updateDropOverlayState(ready);
	setDropOverlayVisible(true);
	if (ready) event->acceptProposedAction();
	else event->ignore();
}

void MainWindow::dragMoveEvent(QDragMoveEvent *event) {
	if (firstLocalFile(event->mimeData()).isEmpty()) {
		event->ignore();
		return;
	}
	if (isReadyToSend()) event->acceptProposedAction();
	else event->ignore();
}

void MainWindow::dragLeaveEvent(QDragLeaveEvent *event) {
	setDropOverlayVisible(false);
	QMainWindow::dragLeaveEvent(event);
}

void MainWindow::dropEvent(QDropEvent *event) {
	setDropOverlayVisible(false);
	const QString path = firstLocalFile(event->mimeData());
	if (path.isEmpty()) return;
	if (!isReadyToSend()) {
		if (!m_connected)
			QMessageBox::warning(this, "Not connected", "Connect to a peer before sending files.");
		return;
	}
	event->acceptProposedAction();
	qInfo() << "File dropped to send:" << path;
	ui->cancelButton->setEnabled(true);
	ui->statusLabel->setText("Preparing...");
	QMetaObject::invokeMethod(m_fileManager, [mgr = m_fileManager, path]() { mgr->sendFile(path); });
}

void MainWindow::resizeEvent(QResizeEvent *event) {
	QMainWindow::resizeEvent(event);
	if (m_dropOverlay) m_dropOverlay->setGeometry(rect());
}
