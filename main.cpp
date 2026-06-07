#include "mainwindow.h"
#include <QApplication>
#include <QStyleFactory>
#include <QDateTime>
#include <QTextStream>
#include <QMutex>
#include <QMutexLocker>
#include <QStyle>

#ifdef Q_OS_WIN
#include <windows.h>
#include <dwmapi.h>
static void applyDarkTitleBar(WId winId) {
	HWND hwnd = reinterpret_cast<HWND>(winId);
	BOOL dark = TRUE;
	DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark));
	COLORREF caption = RGB(0x12, 0x13, 0x19);
	DwmSetWindowAttribute(hwnd, 35, &caption, sizeof(caption));
}
#endif

static QMutex logMutex;

void LogMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg) {
	QMutexLocker locker(&logMutex);
	QFile outFile("p2pclient.log");

	if (!outFile.open(QIODevice::WriteOnly | QIODevice::Append)) return;

	QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz");
	QString level;

	switch (type) {
		case QtDebugMsg:    level = "[DEBUG]"; break;
		case QtInfoMsg:     level = "[INFO ]"; break;
		case QtWarningMsg:  level = "[WARN ]"; break;
		case QtCriticalMsg: level = "[ERROR]"; break;
		case QtFatalMsg:    level = "[FATAL]"; break;
	}

	QTextStream textStream(&outFile);
	QTextStream console(stdout);

	QString formattedMessage = QString("[%1] %2 %3\n").arg(timestamp, level, msg);
	textStream << formattedMessage;
	console << formattedMessage;
}

int main(int argc, char *argv[]) {
	qInstallMessageHandler(LogMessageHandler);
	QApplication a(argc, argv);
	QApplication::setStyle(QStyleFactory::create("Fusion"));
	a.setWindowIcon(a.style()->standardIcon(QStyle::SP_DriveNetIcon));
	qInfo() << "Starting client";
	MainWindow w;
	w.show();
#ifdef Q_OS_WIN
	applyDarkTitleBar(w.winId());
#endif
	return a.exec();
}
