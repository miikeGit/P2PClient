#include "appconfig.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>

AppConfig AppConfig::load(const QString& filePath) {
	AppConfig config;
	QFile file(filePath);

	if (file.open(QIODevice::ReadOnly)) {
		qDebug() << "Successfully opened config file:" << filePath;
		QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
		file.close();
		QJsonObject root = doc.object();

		if (root.contains("mqtt")) {
			qDebug() << "Parsing MQTT configuration...";
			QJsonObject mqttObj = root["mqtt"].toObject();
			config.mqtt.host = mqttObj["host"].toString();
			config.mqtt.port = mqttObj["port"].toInt();
		}
		else {
			qWarning() << "MQTT section is missing in config.json!";
		}

		if (root.contains("webrtc")) {
			qDebug() << "Parsing WebRTC configuration...";
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
				qInfo() << "Loaded" << config.iceServers.size() << "ICE server(s) from config";
			}
			else {
				qWarning() << "WebRTC section is missing in config.json!";
			}
		}
		qInfo() << "AppConfig loaded successfully";
	}
	else {
		qCritical() << "Could not open config file at" << filePath;
	}
	return config;
}