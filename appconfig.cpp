#include "appconfig.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>

AppConfig AppConfig::load(const QString& filePath) {
	AppConfig config;
	QFile file(filePath);
	QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
	QJsonObject root = doc.object();

	if (root.contains("mqtt")) {
		QJsonObject mqttObj = root["mqtt"].toObject();
		config.mqtt.host = mqttObj["host"].toString();
		config.mqtt.port = mqttObj["port"].toInt();
		config.mqtt.username = mqttObj["username"].toString();
		config.mqtt.password = mqttObj["password"].toString();
	}

	if (root.contains("webrtc")) {
		QJsonObject webrtcObj = root["webrtc"].toObject();
		if (webrtcObj.contains("ice_servers")) {
			QJsonArray serversArr = webrtcObj["ice_servers"].toArray();
			for (const QJsonValue& val : serversArr) {
				QJsonObject server = val.toObject();
				IceServerConfig ice;
				ice.urls = server["urls"].toString();
				ice.username = server["username"].toString();
				ice.password = server["password"].toString();
				config.iceServers.append(ice);
			}
		}
	}
	return config;
}