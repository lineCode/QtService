#include "basicservicetest.h"
#include <QtTest/QtTest>
#include <QDateTime>
using namespace QtService;

BasicServiceTest::BasicServiceTest(QObject *parent) :
	QObject(parent)
{}

void BasicServiceTest::initTestCase()
{
	qDebug() << QDateTime::currentDateTime();

#ifdef Q_OS_LINUX
	if(!qgetenv("LD_PRELOAD").contains("Qt5Service"))
		qWarning() << "No LD_PRELOAD set - this may fail on systems with multiple version of the modules";
#endif
	init();
	control = ServiceControl::create(backend(), name(), this);
	QVERIFY(control);
	QVERIFY2(control->serviceExists(), qUtf8Printable(control->error()));
	control->setBlocking(true);
}

void BasicServiceTest::cleanupTestCase()
{
	qDebug() << QDateTime::currentDateTime();

	if(control)
		control->stop();
	cleanup();
}

void BasicServiceTest::testStart()
{
	qDebug() << QDateTime::currentDateTime();

	testFeature(ServiceControl::SupportsStatus);
	QCOMPARE(control->status(), ServiceControl::ServiceStopped);

	testFeature(ServiceControl::SupportsStart);
	QVERIFY2(control->start(), qUtf8Printable(control->error()));
	// blocking should only return after the server started, but for non blocking this may not be the case...
	if(!control->supportFlags().testFlag(ServiceControl::SupportsBlocking))
		QThread::sleep(3);

	socket = new QLocalSocket(this);
	socket->connectToServer(QStringLiteral("__qtservice_testservice"));
	QVERIFY(socket->waitForConnected(30000));
	stream.setDevice(socket);

	QByteArray msg;
	READ_LOOP(msg);
	QCOMPARE(msg, QByteArray("started"));

	testFeature(ServiceControl::SupportsStatus);
	QCOMPARE(control->status(), ServiceControl::ServiceRunning);
}

void BasicServiceTest::testReload()
{
	qDebug() << QDateTime::currentDateTime();

	testFeature(ServiceControl::SupportsStatus);
	QCOMPARE(control->status(), ServiceControl::ServiceRunning);

	testFeature(ServiceControl::SupportsReload);
	QVERIFY2(control->reload(), qUtf8Printable(control->error()));

	QByteArray msg;
	READ_LOOP(msg);
	QCOMPARE(msg, QByteArray("reloading"));

	testFeature(ServiceControl::SupportsStatus);
	QCOMPARE(control->status(), ServiceControl::ServiceRunning);
}

void BasicServiceTest::testPause()
{
	qDebug() << QDateTime::currentDateTime();

	testFeature(ServiceControl::SupportsStatus);
	QCOMPARE(control->status(), ServiceControl::ServiceRunning);

	testFeature(ServiceControl::SupportsPause);
	QVERIFY2(control->pause(), qUtf8Printable(control->error()));

	QByteArray msg;
	READ_LOOP(msg);
	QCOMPARE(msg, QByteArray("pausing"));

	testFeature(ServiceControl::SupportsStatus);
	QCOMPARE(control->status(), ServiceControl::ServicePaused);
}

void BasicServiceTest::testResume()
{
	qDebug() << QDateTime::currentDateTime();

	testFeature(ServiceControl::SupportsStatus);
	testFeature(ServiceControl::SupportsResume);
	QCOMPARE(control->status(), ServiceControl::ServicePaused);

	testFeature(ServiceControl::SupportsResume);
	QVERIFY2(control->resume(), qUtf8Printable(control->error()));

	QByteArray msg;
	READ_LOOP(msg);
	QCOMPARE(msg, QByteArray("resuming"));

	testFeature(ServiceControl::SupportsStatus);
	QCOMPARE(control->status(), ServiceControl::ServiceRunning);
}

void BasicServiceTest::testCustom()
{
	qDebug() << QDateTime::currentDateTime();

	testFeature(ServiceControl::SupportsCustomCommands);
	testCustomImpl();
}

void BasicServiceTest::testStop()
{
	qDebug() << QDateTime::currentDateTime();

	testFeature(ServiceControl::SupportsStatus);
	QCOMPARE(control->status(), ServiceControl::ServiceRunning);

	testFeature(ServiceControl::SupportsStop);
	QVERIFY2(control->stop(), qUtf8Printable(control->error()));

#ifndef Q_OS_WIN
	QByteArray msg;
	READ_LOOP(msg);
	QCOMPARE(msg, QByteArray("stopping"));
#endif
	QVERIFY(socket->waitForDisconnected(5000));

	testFeature(ServiceControl::SupportsStatus);
	QCOMPARE(control->status(), ServiceControl::ServiceStopped);
}

void BasicServiceTest::testAutostart()
{
	QVERIFY2(!control->isAutostartEnabled(), qUtf8Printable(control->error()));

	testFeature(ServiceControl::SupportsSetAutostart);
	QVERIFY2(control->enableAutostart(), qUtf8Printable(control->error()));
	QVERIFY2(control->isAutostartEnabled(), qUtf8Printable(control->error()));
	QVERIFY2(control->disableAutostart(), qUtf8Printable(control->error()));
	QVERIFY2(!control->isAutostartEnabled(), qUtf8Printable(control->error()));
}

QString BasicServiceTest::name()
{
	return QStringLiteral("testservice");
}

void BasicServiceTest::init() {}

void BasicServiceTest::cleanup() {}

void BasicServiceTest::testCustomImpl()
{
	QVERIFY2(false, "testCustomImpl not implemented");
}

void BasicServiceTest::performSocketTest()
{
	qDebug() << QDateTime::currentDateTime();

	testFeature(ServiceControl::SupportsStatus);
	QCOMPARE(control->status(), ServiceControl::ServiceStopped);

	auto tcpSocket = new QTcpSocket(this);
	tcpSocket->connectToHost(QStringLiteral("127.0.0.1"), 15843);
	QVERIFY(tcpSocket->waitForConnected(5000));
	while(control->status() == ServiceControl::ServiceStarting)
		QThread::msleep(500);
	testFeature(ServiceControl::SupportsStatus);
	QCOMPARE(control->status(), ServiceControl::ServiceRunning);

	QByteArray msg = "hello world";
	tcpSocket->write(msg);

	QByteArray resMsg;
	do {
		QVERIFY(tcpSocket->waitForReadyRead(5000));
		resMsg += tcpSocket->readAll();
	} while(resMsg.size() < msg.size());
	QCOMPARE(resMsg, msg);

	testFeature(ServiceControl::SupportsStop);
	QVERIFY2(control->stop(), qUtf8Printable(control->error()));
	testFeature(ServiceControl::SupportsStatus);
	QCOMPARE(control->status(), ServiceControl::ServiceStopped);
}

void BasicServiceTest::testFeature(ServiceControl::SupportFlag flag)
{
	if(!control->supportFlags().testFlag(flag)) {
		auto meta = QMetaEnum::fromType<ServiceControl::SupportFlags>();
		if(flag == ServiceControl::SupportsStatus) {
			QEXPECT_FAIL("",
						 QByteArray(QByteArrayLiteral("feature ") + meta.valueToKey(flag) + QByteArrayLiteral(" not supported by backend")).constData(),
						 Continue);
		} else {
			QEXPECT_FAIL("",
						 QByteArray(QByteArrayLiteral("feature ") + meta.valueToKey(flag) + QByteArrayLiteral(" not supported by backend")).constData(),
						 Abort);
		}
	}
}
