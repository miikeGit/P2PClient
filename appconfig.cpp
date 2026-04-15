#include "appconfig.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>
#include <QStandardPaths>

AppConfig AppConfig::load(const QString& filePath) {
	AppConfig config;
	config.downloadPath = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);

	QFile file(filePath);
	if (!file.open(QIODevice::ReadOnly)) {
		qCritical() << "Could not open config file at" << filePath;
		return config;
	}

	qDebug() << "Successfully opened config file:" << filePath;
	QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();

	if (auto mqttObj = root["mqtt"].toObject(); !mqttObj.isEmpty()) {
		qDebug() << "Parsing MQTT configuration...";
		config.mqtt.host = mqttObj["host"].toString();
		config.mqtt.port = mqttObj["port"].toInt();
	} else {
		qWarning() << "MQTT section is missing in config.json!";
	}

	if (auto webrtcObj = root["webrtc"].toObject(); webrtcObj.contains("ice_servers")) {
		qDebug() << "Parsing WebRTC configuration...";
		for (const auto& val : webrtcObj["ice_servers"].toArray()) {
			auto server = val.toObject();
			config.iceServers.append({server["urls"].toString(), server["username"].toString(), server["password"].toString()});
		}
		qInfo() << "Loaded" << config.iceServers.size() << "ICE server(s) from config";
	} else {
		qWarning() << "WebRTC section is missing in config.json!";
	}

	if (auto settingsObj = root["settings"].toObject(); settingsObj.contains("download_path")) {
		config.downloadPath = settingsObj["download_path"].toString();
		qInfo() << "Successfully loaded download path from config.json!";
	} else {
		qWarning() << "Download path entry is missing in config.json!";
	}

	qInfo() << "AppConfig loaded successfully";
	return config;
}

bool AppConfig::save(const QString &filePath) const {
	QFile file(filePath);
	QJsonObject root;

	if (file.open(QIODevice::ReadOnly)) {
		root = QJsonDocument::fromJson(file.readAll()).object();
		file.close();
	}

	QJsonObject settingsObj = root["settings"].toObject();
	settingsObj["download_path"] = downloadPath;
	root["settings"] = settingsObj;

	if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		file.write(QJsonDocument(root).toJson());
		return true;
	}

	qCritical() << "Failed to save config file to" << filePath;
	return false;
}