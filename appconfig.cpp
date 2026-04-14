#include "appconfig.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>
#include <QStandardPaths>

AppConfig AppConfig::load(const QString& filePath) {
	AppConfig config;
	QFile file(filePath);
	config.downloadPath = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);

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

		if (root.contains("settings")) {
			QJsonObject settingsObj = root["settings"].toObject();
			if (settingsObj.contains("download_path")) {
				config.downloadPath = settingsObj["download_path"].toString();
				qInfo() << "Successfully loaded download path from config.json!";
			}
			else {
				qWarning() << "Download path entry is missing in config.json!";
			}
		}
		qInfo() << "AppConfig loaded successfully";
	}
	else {
		qCritical() << "Could not open config file at" << filePath;
	}
	return config;
}

bool AppConfig::save(const QString &filePath) const {
	QFile file(filePath);
	QJsonObject root;

	if (file.open(QIODevice::ReadOnly)) {
		qDebug() << "Successfully opened config file:" << filePath;
		QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
		if (!doc.isNull() && doc.isObject()) {
			root = doc.object();
		}
		file.close();
	}

	QJsonObject settingsObj;
	if (root.contains("settings")) {
		settingsObj = root["settings"].toObject();
	}
	settingsObj["download_path"] = downloadPath;
	root["settings"] = settingsObj;

	if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		QJsonDocument doc(root);
		file.write(doc.toJson());
		file.close();
		return true;
	}

	qCritical() << "Failed to save config file to" << filePath;
	return false;
}