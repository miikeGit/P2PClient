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
		QString downloadPath;

		static AppConfig load(const QString &filePath);
		bool save(const QString &filePath) const;
};

#endif // APPCONFIG_H