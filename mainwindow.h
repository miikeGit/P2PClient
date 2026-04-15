#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QApplication>
#include <memory>
#include "p2pclient.h"
#include "filetransfermanager.h"
#include <QMap>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

struct TransferUI {
	QWidget* container;
	QLabel* nameLabel;
	QProgressBar* progressBar;
	QLabel* statusLabel;
	QPushButton* cancelButton;
};

class MainWindow : public QMainWindow {
	Q_OBJECT
public:
	MainWindow(QWidget *parent = nullptr);
	~MainWindow();

private slots:
	void on_callButton_clicked();
	void on_sendFileButton_clicked();
	void on_copyIdButton_clicked();
	void on_selectDownloadPathButton_clicked();

private:
	std::unique_ptr<Ui::MainWindow> ui;
	P2PClient* m_p2pClient = nullptr;
	
	QMap<int, FileTransferManager*> m_transfers;
	QMap<int, TransferUI> m_transferUIs;
	int m_nextTransferId = 1;

	QString m_configPath = qApp->applicationDirPath() + "/config.json";
	AppConfig m_appConfig;

	FileTransferManager* createTransferManager(int id);
	void cleanupTransfer(int id);
	void updateSpeedLimits();
};

#endif // MAINWINDOW_H