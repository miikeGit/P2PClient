#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QApplication>
#include <QThread>
#include <memory>
#include "p2pclient.h"
#include "filetransfermanager.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
class QDragEnterEvent;
class QDragMoveEvent;
class QDragLeaveEvent;
class QDropEvent;
class QResizeEvent;
class QMimeData;
class QLabel;
class QGraphicsOpacityEffect;
class QPropertyAnimation;
QT_END_NAMESPACE

class MainWindow : public QMainWindow {
	Q_OBJECT
public:
	MainWindow(QWidget *parent = nullptr);
	~MainWindow();

protected:
	void dragEnterEvent(QDragEnterEvent *event) override;
	void dragMoveEvent(QDragMoveEvent *event) override;
	void dragLeaveEvent(QDragLeaveEvent *event) override;
	void dropEvent(QDropEvent *event) override;
	void resizeEvent(QResizeEvent *event) override;

private slots:
	void on_callButton_clicked();
	void on_sendFileButton_clicked();
	void on_cancelButton_clicked();
	void on_copyIdButton_clicked();
	void on_selectDownloadPathButton_clicked();

private:
	std::unique_ptr<Ui::MainWindow> ui;
	P2PClient* m_p2pClient = nullptr;
	FileTransferManager* m_fileManager = nullptr;
	QThread* m_workerThread = nullptr;

	QString m_configPath = qApp->applicationDirPath() + "/config.json";
	AppConfig m_appConfig;

	QWidget* m_dropOverlay = nullptr;
	QLabel* m_dropIcon = nullptr;
	QLabel* m_dropTitle = nullptr;
	QLabel* m_dropSubtitle = nullptr;
	QGraphicsOpacityEffect* m_dropOverlayOpacity = nullptr;
	QPropertyAnimation* m_dropOverlayAnim = nullptr;

	bool m_connected = false;
	bool m_transferActive = false;

	void wireSignals();
	void setConnectionStatus(bool online);
	void clearFileInfo();

	void buildDropOverlay();
	void setDropOverlayVisible(bool visible);
	void updateDropOverlayState(bool ready);
	bool isReadyToSend() const { return m_connected && !m_transferActive; }
	QString firstLocalFile(const QMimeData *mime) const;
};

#endif // MAINWINDOW_H
