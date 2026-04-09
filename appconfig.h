#ifndef APPCONFIG_H
#define APPCONFIG_H

#include <QString>
#include <QList>
#include <QJsonArray>

struct MqttConfig {
		QString host;
		quint16 port;
		QString username;
		QString password;
};

struct IceServerConfig {
		QString urls;
		QString username;
		QString password;
};

struct AppConfig {
		MqttConfig mqtt;
		QList<IceServerConfig> iceServers;
		static void extracted(AppConfig &config, QJsonArray &serversArr);
		static AppConfig load(const QString &filePath);
};

#endif // APPCONFIG_H