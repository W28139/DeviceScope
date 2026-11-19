#include "EchoServer.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDebug>
#include <QHostAddress>

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("DeviceScope Simulator"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    QCoreApplication::setOrganizationName(QStringLiteral("DeviceScope"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("DeviceScope device simulator (step 0: TCP echo server)"));
    parser.addHelpOption();
    parser.addVersionOption();

    const QCommandLineOption hostOption(
        {QStringLiteral("H"), QStringLiteral("host")},
        QStringLiteral("Listen address."),
        QStringLiteral("address"),
        QStringLiteral("127.0.0.1"));
    const QCommandLineOption portOption(
        {QStringLiteral("p"), QStringLiteral("port")},
        QStringLiteral("Listen port."),
        QStringLiteral("port"),
        QStringLiteral("9000"));
    parser.addOptions({hostOption, portOption});
    parser.process(app);

    bool portOk = false;
    const uint port = parser.value(portOption).toUInt(&portOk);
    if (!portOk || port > 65535) {
        qCritical() << "invalid port:" << parser.value(portOption);
        return 2;
    }

    QHostAddress address;
    if (!address.setAddress(parser.value(hostOption))) {
        qCritical() << "invalid host:" << parser.value(hostOption);
        return 2;
    }

    EchoServer server;
    if (!server.listen(address, static_cast<quint16>(port))) {
        return 1;
    }

    return app.exec();
}