#include <QtTest>
#include <QSignalSpy>
#include <QFile>
#include <QStandardPaths>
#include <QProcess>
#include <QThread>

#include "appconfig.h"
#include "filetransfermanager.h"
#include "p2pclient.h"

class P2PTests : public QObject {
	Q_OBJECT

private:
	void wireUp(P2PClient& client, FileTransferManager& fm, int transferId) {
		connect(&fm, &FileTransferManager::sendBinaryData, &client, [&client, transferId](const QByteArray& data) {
			QByteArray pkt;
			pkt.append(reinterpret_cast<const char*>(&transferId), sizeof(transferId));
			pkt.append(data);
			client.sendBinary(pkt);
		});
		connect(&fm, &FileTransferManager::sendJsonCommand, &client, [&client, transferId](QJsonObject json) {
			json["transfer_id"] = transferId;
			client.sendJson(json);
		});
		connect(&client, &P2PClient::binaryReceived, &fm, [&fm, transferId](const QByteArray& data) {
			if (data.size() < 4) return;
			int id = *reinterpret_cast<const int*>(data.constData());
			if (id == transferId) {
				fm.handleBinaryChunk(data.mid(4));
			}
		});
		connect(&client, &P2PClient::jsonReceived, &fm, [&fm, transferId](const QJsonObject& json) {
			int id = json["transfer_id"].toInt();
			if (id == transferId) {
				fm.handleJsonCommand(json);
			}
		});
	}

private slots:

	QString calculateTestHash(const QString &filePath) {
		QFile file(filePath);
		if (!file.open(QIODevice::ReadOnly)) {
			return QString();
		}

		QCryptographicHash hash(QCryptographicHash::Sha256);
		if (hash.addData(&file)) {
			return QString(hash.result().toHex());
		}
		return QString();
	}

	void testConnectAndTransfer() {
		AppConfig config = AppConfig::load("config.json");

		P2PClient clientA(config);
		FileTransferManager clientAFM;
		clientAFM.setDownloadPath(QStandardPaths::writableLocation(QStandardPaths::DownloadLocation));
		wireUp(clientA, clientAFM, 1);

		P2PClient clientB(config);
		FileTransferManager clientBFM;
		clientBFM.setDownloadPath(QStandardPaths::writableLocation(QStandardPaths::DownloadLocation));
		wireUp(clientB, clientBFM, 1);

		QSignalSpy clientAMqttSpy(&clientA, &P2PClient::brokerConnected);
		QSignalSpy clientBMqttSpy(&clientB, &P2PClient::brokerConnected);

		clientA.connectToBroker();
		clientB.connectToBroker();

		QVERIFY(clientAMqttSpy.count() > 0 || clientAMqttSpy.wait(5000));
		QVERIFY(clientBMqttSpy.count() > 0 || clientBMqttSpy.wait(5000));

		QSignalSpy clientARtcSpy(&clientA, &P2PClient::connectionEstablished);
		QSignalSpy clientBRtcSpy(&clientB, &P2PClient::connectionEstablished);

		clientA.call(clientB.getMyId());

		QVERIFY2(clientARtcSpy.count() > 0 || clientARtcSpy.wait(10000), "Client A connection timeout");
		QVERIFY2(clientBRtcSpy.count() > 0 || clientBRtcSpy.wait(10000), "Client B connection timeout");

		QString testFileName = "test_5mb_file.bin";
		QFile testFile(testFileName);
		QVERIFY(testFile.open(QIODevice::WriteOnly));
		testFile.write(QByteArray(5 * 1024 * 1024, 'A'));
		testFile.close();

		QSignalSpy clientBTransferFinishedSpy(&clientBFM, &FileTransferManager::transferFinished);
		clientAFM.sendFile(testFileName);

		QVERIFY2(clientBTransferFinishedSpy.wait(15000), "File transfer timeout");

		QString originalHash = calculateTestHash(testFileName);
		QString downloadedFilePath = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation) + "/" + testFileName;
		QString downloadedHash = calculateTestHash(downloadedFilePath);

		QCOMPARE(originalHash, downloadedHash);

		QFile::remove(testFileName);
		QFile::remove(downloadedFilePath);
	}

	void testCancelTransferLogic() {
		AppConfig config = AppConfig::load("config.json");
		P2PClient clientA(config); FileTransferManager clientAFM;
		P2PClient clientB(config); FileTransferManager clientBFM;
		clientAFM.setDownloadPath(QStandardPaths::writableLocation(QStandardPaths::DownloadLocation));
		clientBFM.setDownloadPath(QStandardPaths::writableLocation(QStandardPaths::DownloadLocation));

		wireUp(clientA, clientAFM, 1);
		wireUp(clientB, clientBFM, 1);

		clientA.connectToBroker(); clientB.connectToBroker();
		QSignalSpy clientAMqttSpy(&clientA, &P2PClient::brokerConnected);
		QSignalSpy clientBMqttSpy(&clientB, &P2PClient::brokerConnected);
		clientAMqttSpy.wait(5000); clientBMqttSpy.wait(5000);

		clientA.call(clientB.getMyId());
		QSignalSpy clientBRtcSpy(&clientB, &P2PClient::connectionEstablished);
		clientBRtcSpy.wait(10000);

		QString testFileName = "test_cancel_10mb.bin";
		QFile testFile(testFileName);
		testFile.open(QIODevice::WriteOnly);
		testFile.write(QByteArray(10 * 1024 * 1024, 'B'));
		testFile.close();

		QSignalSpy clientBCanceledSpy(&clientBFM, &FileTransferManager::transferCanceled);

		clientAFM.sendFile(testFileName);

		QThread::msleep(100);

		clientAFM.cancelTransfer();

		QVERIFY2(clientBCanceledSpy.wait(5000), "Cancel signal timeout");

		QString downloadedFilePath = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation) + "/" + testFileName;
		QVERIFY2(!QFile::exists(downloadedFilePath), "file was not deleted");

		QFile::remove(testFileName);
	}

	void testClumsyBadNetwork() {
		QProcess clumsyProcess;
		QStringList args = {"--filter", "udp", "--drop", "on", "--drop-chance", "5.0"};
		clumsyProcess.start("clumsy.exe", args);
		clumsyProcess.waitForStarted(2000);

		AppConfig config = AppConfig::load("config.json");
		P2PClient clientA(config); FileTransferManager clientAFM;
		P2PClient clientB(config); FileTransferManager clientBFM;
		clientAFM.setDownloadPath(QStandardPaths::writableLocation(QStandardPaths::DownloadLocation));
		clientBFM.setDownloadPath(QStandardPaths::writableLocation(QStandardPaths::DownloadLocation));

		wireUp(clientA, clientAFM, 1);
		wireUp(clientB, clientBFM, 1);

		clientA.connectToBroker(); clientB.connectToBroker();
		QSignalSpy clientAMqttSpy(&clientA, &P2PClient::brokerConnected); clientAMqttSpy.wait(5000);
		QSignalSpy clientBMqttSpy(&clientB, &P2PClient::brokerConnected); clientBMqttSpy.wait(5000);

		clientA.call(clientB.getMyId());
		QSignalSpy clientBRtcSpy(&clientB, &P2PClient::connectionEstablished);
		QVERIFY2(clientBRtcSpy.wait(15000), "Connection failed under packet loss");

		QString testFileName = "test_clumsy_1mb.txt";
		QFile testFile(testFileName);
		testFile.open(QIODevice::WriteOnly);
		testFile.write(QByteArray(1 * 1024 * 1024, 'C'));
		testFile.close();

		QSignalSpy clientBTransferFinishedSpy(&clientBFM, &FileTransferManager::transferFinished);
		clientAFM.sendFile(testFileName);

		QVERIFY2(clientBTransferFinishedSpy.wait(30000), "File transfer timeout");

		QString originalHash = calculateTestHash(testFileName);
		QString downloadedFilePath = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation) + "/" + testFileName;
		QString downloadedHash = calculateTestHash(downloadedFilePath);

		QCOMPARE(originalHash, downloadedHash);

		clumsyProcess.terminate();
		clumsyProcess.waitForFinished();

		QFile::remove(testFileName);
		QFile::remove(downloadedFilePath);
	}

	void testPeerDropDuringTransfer() {
		AppConfig config = AppConfig::load("config.json");
		P2PClient clientA(config); FileTransferManager clientAFM;
		P2PClient clientB(config); FileTransferManager clientBFM;
		clientAFM.setDownloadPath(QStandardPaths::writableLocation(QStandardPaths::DownloadLocation));
		clientBFM.setDownloadPath(QStandardPaths::writableLocation(QStandardPaths::DownloadLocation));

		connect(&clientA, &P2PClient::connectionClosed, &clientAFM, &FileTransferManager::onPeerDisconnected);
		connect(&clientB, &P2PClient::connectionClosed, &clientBFM, &FileTransferManager::onPeerDisconnected);
		wireUp(clientA, clientAFM, 1);
		wireUp(clientB, clientBFM, 1);

		clientA.connectToBroker(); clientB.connectToBroker();
		QSignalSpy aMqtt(&clientA, &P2PClient::brokerConnected);
		QSignalSpy bMqtt(&clientB, &P2PClient::brokerConnected);
		QVERIFY(aMqtt.count() > 0 || aMqtt.wait(5000));
		QVERIFY(bMqtt.count() > 0 || bMqtt.wait(5000));

		clientA.call(clientB.getMyId());
		QSignalSpy aRtc(&clientA, &P2PClient::connectionEstablished);
		QSignalSpy bRtc(&clientB, &P2PClient::connectionEstablished);
		QVERIFY(aRtc.count() > 0 || aRtc.wait(10000));
		QVERIFY(bRtc.count() > 0 || bRtc.wait(10000));

		QString testFileName = "drop_test.txt";
		QFile testFile(testFileName);
		testFile.open(QIODevice::WriteOnly);
		testFile.write(QByteArray(5 * 1024 * 1024, 'X'));
		testFile.close();

		QSignalSpy clientACanceledSpy(&clientAFM, &FileTransferManager::transferCanceled);
		QSignalSpy clientBCanceledSpy(&clientBFM, &FileTransferManager::transferCanceled);

		clientAFM.sendFile(testFileName);

		QThread::msleep(300);

		clientB.closeConnection();

		QVERIFY2(clientACanceledSpy.count() > 0 || clientACanceledSpy.wait(5000), "Transfer not canceled after disconnect");
		QVERIFY2(clientBCanceledSpy.count() > 0 || clientBCanceledSpy.wait(5000), "Resources not cleaned up after disconnect");

		QString downloadedPath = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation) + "/" + testFileName;
		QVERIFY2(!QFile::exists(downloadedPath), "Corrupted file remained on disk");

		QFile::remove(testFileName);
	}

	void testHashMismatchIntegrity() {
		AppConfig config = AppConfig::load("config.json");
		P2PClient clientA(config); FileTransferManager clientAFM;
		P2PClient clientB(config); FileTransferManager clientBFM;
		clientAFM.setDownloadPath(QStandardPaths::writableLocation(QStandardPaths::DownloadLocation));
		clientBFM.setDownloadPath(QStandardPaths::writableLocation(QStandardPaths::DownloadLocation));

		wireUp(clientA, clientAFM, 1);
		wireUp(clientB, clientBFM, 1);

		clientA.connectToBroker(); clientB.connectToBroker();
		QSignalSpy aMqtt(&clientA, &P2PClient::brokerConnected);
		QSignalSpy bMqtt(&clientB, &P2PClient::brokerConnected);
		QVERIFY(aMqtt.count() > 0 || aMqtt.wait(5000));
		QVERIFY(bMqtt.count() > 0 || bMqtt.wait(5000));

		clientA.call(clientB.getMyId());

		QSignalSpy aRtc(&clientA, &P2PClient::connectionEstablished);
		QSignalSpy bRtc(&clientB, &P2PClient::connectionEstablished);
		QVERIFY(aRtc.count() > 0 || aRtc.wait(10000));
		QVERIFY(bRtc.count() > 0 || bRtc.wait(10000));

		QByteArray realData = "This is correct data";

		QJsonObject maliciousMeta;
		maliciousMeta["file_name"] = "integrity_test.txt";
		maliciousMeta["file_size"] = (qint64)realData.size();
		maliciousMeta["file_hash"] = "fake_hash_123456789";
		maliciousMeta["transfer_id"] = 1;

		clientA.sendJson(maliciousMeta);

		QThread::msleep(100);

		QByteArray pkt;
		int transferId = 1;
		pkt.append(reinterpret_cast<const char*>(&transferId), sizeof(transferId));
		pkt.append(realData);
		clientA.sendBinary(pkt);

		QSignalSpy clientBCanceledSpy(&clientBFM, &FileTransferManager::transferCanceled);
		QSignalSpy clientBFinishedSpy(&clientBFM, &FileTransferManager::transferFinished);

		QVERIFY2(clientBCanceledSpy.count() > 0 || clientBCanceledSpy.wait(5000), "Invalid hash not rejected");
		QCOMPARE(clientBFinishedSpy.count(), 0);

		QString downloadedPath = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation) + "/integrity_test.txt";
		QVERIFY2(!QFile::exists(downloadedPath), "File with invalid hash saved to disk");
	}
};

int main(int argc, char *argv[]) {
	QCoreApplication app(argc, argv);
	P2PTests tc;
	return QTest::qExec(&tc, argc, argv);
}

#include "test_main.moc"