#include "SimulatorServer.h"

#include <QtCore/QCommandLineParser>
#include <QtCore/QCoreApplication>
#include <QtCore/QDebug>
#include <QtCore/QLoggingCategory>
#include <QtCore/QTimer>
#include <QtNetwork/QHostAddress>

#include <csignal>

namespace {

volatile std::sig_atomic_t g_stopRequested = 0;

void handleSignal(int)
{
    g_stopRequested = 1;
}

bool applyLogLevel(const QString &level)
{
    if (level == QStringLiteral("debug")) {
        QLoggingCategory::setFilterRules(QStringLiteral("app.sim.debug=true"));
    } else if (level == QStringLiteral("info")) {
        QLoggingCategory::setFilterRules(QStringLiteral("app.sim.debug=false"));
    } else if (level == QStringLiteral("warning")) {
        QLoggingCategory::setFilterRules(
            QStringLiteral("app.sim.debug=false\napp.sim.info=false"));
    } else if (level == QStringLiteral("critical")) {
        QLoggingCategory::setFilterRules(
            QStringLiteral("app.sim.debug=false\napp.sim.info=false\napp.sim.warning=false"));
    } else {
        return false;
    }
    return true;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("DeviceScope Simulator"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    QCoreApplication::setOrganizationName(QStringLiteral("DeviceScope"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("DeviceScope device simulator"));
    parser.addHelpOption();
    parser.addVersionOption();

    const QCommandLineOption hostOption(
        {QStringLiteral("H"), QStringLiteral("host")},
        QStringLiteral("Listen address."), QStringLiteral("address"),
        QStringLiteral("127.0.0.1"));
    const QCommandLineOption portOption(
        {QStringLiteral("p"), QStringLiteral("port")},
        QStringLiteral("Listen port."), QStringLiteral("port"), QStringLiteral("9000"));
    const QCommandLineOption rateOption(
        QStringLiteral("rate"), QStringLiteral("Telemetry rate in Hz."),
        QStringLiteral("hz"), QStringLiteral("10"));
    const QCommandLineOption seedOption(
        QStringLiteral("seed"), QStringLiteral("Random seed."),
        QStringLiteral("seed"), QStringLiteral("1"));
    const QCommandLineOption logLevelOption(
        QStringLiteral("log-level"),
        QStringLiteral("Log level: debug, info, warning, critical."),
        QStringLiteral("level"), QStringLiteral("info"));

    parser.addOptions({hostOption, portOption, rateOption, seedOption, logLevelOption});
    parser.process(app);

    if (!applyLogLevel(parser.value(logLevelOption).toLower())) {
        qCritical() << "invalid log level:" << parser.value(logLevelOption);
        return 2;
    }

    bool ok = false;
    const uint port = parser.value(portOption).toUInt(&ok);
    if (!ok || port > 65535) {
        qCritical() << "invalid port:" << parser.value(portOption);
        return 2;
    }

    const int rate = parser.value(rateOption).toInt(&ok);
    if (!ok || rate < 1 || rate > 1000) {
        qCritical() << "invalid rate:" << parser.value(rateOption);
        return 2;
    }

    const uint seed = parser.value(seedOption).toUInt(&ok);
    if (!ok) {
        qCritical() << "invalid seed:" << parser.value(seedOption);
        return 2;
    }

    QHostAddress address;
    if (!address.setAddress(parser.value(hostOption))) {
        qCritical() << "invalid host:" << parser.value(hostOption);
        return 2;
    }

    devicescope::simulator::SimulatorConfig config;
    config.address = address;
    config.port = static_cast<quint16>(port);
    config.rateHz = rate;
    config.seed = seed;

    devicescope::simulator::SimulatorServer server(config);
    if (!server.listen()) {
        return 1;
    }

    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    QTimer stopTimer;
    QObject::connect(&stopTimer, &QTimer::timeout, &app, [&server, &app] {
        if (g_stopRequested != 0) {
            server.stop();
            app.quit();
        }
    });
    stopTimer.start(100);

    return app.exec();
}