#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <memory>
#include <QMainWindow>
#include <qmqtt.h>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow {
	Q_OBJECT

public:
	MainWindow(QWidget *parent = nullptr);
	~MainWindow();

private slots:
	void on_connectButton_clicked();
	void on_sendButton_clicked();
	void onMQTTConnected();
	void onMQTTReceived(const QMQTT::Message& message);

private:
	std::unique_ptr<Ui::MainWindow> ui;
	QMQTT::Client *m_mqttClient;
	QString m_topic{"topic"};

	void SetupMQTT();
};

#endif // MAINWINDOW_H
