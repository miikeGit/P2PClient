#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <memory>
#include "p2pclient.h"
#include "filetransfermanager.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow {
	Q_OBJECT
public:
	MainWindow(QWidget *parent = nullptr);
	~MainWindow();

private slots:
	void on_callButton_clicked();
	void on_sendFileButton_clicked();
	void on_cancelButton_clicked();
	void on_copyIdButton_clicked();

private:
	std::unique_ptr<Ui::MainWindow> ui;

	QString m_myId;
	P2PClient* m_p2pClient = nullptr;
	FileTransferManager* m_fileManager = nullptr;
};

#endif // MAINWINDOW_H