#include "mainwindow.h"
#include <QApplication>
#include <QDateTime>
#include <QTextStream>
#include <QMutex>
#include <QMutexLocker>

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
		qInfo() << "Starting client";
		MainWindow w;
		w.show();
		return a.exec();
}