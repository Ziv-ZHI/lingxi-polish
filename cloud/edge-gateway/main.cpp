// 工业云边网关：遥测聚合上云 + 云端指令下行
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QDateTime>
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTimer>

#include <algorithm>
#include <cmath>

#ifdef LX_HAVE_MQTT
#include <QtMqtt/QMqttClient>
#include <QtMqtt/QMqttMessage>
#include <QtMqtt/QMqttSubscription>
#endif

#include "comm/UdpControllerClient.h"

namespace {
constexpr int kDefaultIntervalMs = 1000;   // 上云周期：默认 1s 聚合一次
}  // namespace

class EdgeGateway : public QObject {
public:
    EdgeGateway(const QString& deviceId, const QString& broker, quint16 port,
                const QString& httpUrl, int intervalMs, QObject* parent = nullptr)
        : QObject(parent), deviceId_(deviceId), httpUrl_(httpUrl) {
        ctrl_.setController(QHostAddress(QStringLiteral("192.168.1.10")));
        QString err;
        if (!ctrl_.bindTelemetry(lx::protocol::kPortTele, &err)) {
            qWarning() << err;
        }
        connect(&ctrl_, &lx::UdpControllerClient::telemetryReady,
                this, &EdgeGateway::onTelemetry);
        connect(&ctrl_, &lx::UdpControllerClient::alarmRaised,
                this, &EdgeGateway::onAlarm);

        publishTimer_ = new QTimer(this);
        publishTimer_->setInterval(intervalMs);
        connect(publishTimer_, &QTimer::timeout, this, &EdgeGateway::publish);
        publishTimer_->start();

#ifdef LX_HAVE_MQTT
        mqtt_ = new QMqttClient(this);
        mqtt_->setHostname(broker);
        mqtt_->setPort(port);
        mqtt_->setClientId(QStringLiteral("lingxi-%1").arg(deviceId_));
        connect(mqtt_, &QMqttClient::connected, this, [this] {
            const QString cmdTopic = QStringLiteral("lingxi/%1/cmd").arg(deviceId_);
            auto* sub = mqtt_->subscribe(cmdTopic);
            if (!sub) {
                qWarning() << "订阅失败：" << cmdTopic;
                return;
            }
            connect(sub, &QMqttSubscription::messageReceived,
                    this, &EdgeGateway::onCloudCommand);
            qInfo() << "已连接 MQTT Broker，订阅" << cmdTopic;
        });
        connect(mqtt_, &QMqttClient::stateChanged, this, [](QMqttClient::ClientState s) {
            qInfo() << "MQTT 状态：" << s;
        });
        mqtt_->connectToHost();
#else
        qWarning() << "未编译 Qt MQTT 模块，仅使用 HTTP 上报模式";
        Q_UNUSED(broker);
        Q_UNUSED(port);
#endif

        // 每完成一件工件（进度回到 0 且此前为 100）计数一次
        lastProgress_ = 0;
    }

private slots:
    void onTelemetry(const lx::protocol::TelemetryFrame& f) {
        ++frameCount_;
        forceSum_ += std::sqrt(f.force[0] * f.force[0] + f.force[1] * f.force[1] +
                               f.force[2] * f.force[2]);
        forcePeak_ = std::max(forcePeak_, std::sqrt(f.force[0] * f.force[0] +
                                                    f.force[1] * f.force[1] +
                                                    f.force[2] * f.force[2]));
        for (int i = 0; i < lx::protocol::kJointCount; ++i) {
            torqueSum_[i] += std::fabs(f.jointTor[i]);
        }
        tempMax_ = std::max(tempMax_, double(f.sensorTempC));
        lastProgress_ = f.progress;
        lastMode_ = f.mode;
        lastSeq_ = f.seq;

        if (f.progress == 100 && !finishedLatched_) {
            finishedLatched_ = true;
            ++produced_;
        } else if (f.progress < 10) {
            finishedLatched_ = false;
        }
    }

    void onAlarm(const lx::AlarmRecord& a) {
        ++alarmCount_;
        if (a.level == lx::AlarmLevel::Critical) ++criticalCount_;
        lastAlarm_ = a.text;
    }

    void publish() {
        if (frameCount_ == 0) return;

        QJsonObject o;
        o[QStringLiteral("deviceId")] = deviceId_;
        o[QStringLiteral("ts")] = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
        o[QStringLiteral("online")] = ctrl_.isOnline();
        o[QStringLiteral("mode")] = int(lastMode_);
        o[QStringLiteral("progress")] = int(lastProgress_);
        o[QStringLiteral("seq")] = int(lastSeq_);
        o[QStringLiteral("frames")] = int(frameCount_);
        o[QStringLiteral("forceAvg")] = forceSum_ / frameCount_;
        o[QStringLiteral("forcePeak")] = forcePeak_;
        o[QStringLiteral("tempMax")] = tempMax_;
        o[QStringLiteral("produced")] = int(produced_);
        o[QStringLiteral("alarmCount")] = int(alarmCount_);
        o[QStringLiteral("criticalCount")] = int(criticalCount_);
        o[QStringLiteral("lastAlarm")] = lastAlarm_;

        QJsonArray joints;
        for (int i = 0; i < lx::protocol::kJointCount; ++i) {
            joints.append(torqueSum_[i] / frameCount_);   // 关节平均负载，供磨损分析
        }
        o[QStringLiteral("jointTorqueAvg")] = joints;

        const QByteArray payload = QJsonDocument(o).toJson(QJsonDocument::Compact);

#ifdef LX_HAVE_MQTT
        if (mqtt_ && mqtt_->state() == QMqttClient::Connected) {
            mqtt_->publish(QStringLiteral("lingxi/%1/telemetry").arg(deviceId_), payload);
        }
#endif
        if (!httpUrl_.isEmpty()) post(payload);

        // 清零窗口统计
        frameCount_ = 0;
        forceSum_ = 0.0;
        forcePeak_ = 0.0;
        tempMax_ = 0.0;
        for (double& v : torqueSum_) v = 0.0;
    }

#ifdef LX_HAVE_MQTT
    void onCloudCommand(const QMqttMessage& msg) {
        const QJsonObject o = QJsonDocument::fromJson(msg.payload()).object();
        const QString action = o.value(QStringLiteral("action")).toString();
        QString err;
        if (action == QStringLiteral("pause")) {
            ctrl_.sendCommand(lx::protocol::CmdId::Pause, {}, &err);
        } else if (action == QStringLiteral("stop")) {
            ctrl_.sendCommand(lx::protocol::CmdId::Stop, {}, &err);
        } else if (action == QStringLiteral("start")) {
            ctrl_.sendCommand(lx::protocol::CmdId::Start, {}, &err);
        } else {
            qWarning() << "未知云端指令：" << action;
            return;
        }
        if (!err.isEmpty()) qWarning() << "指令执行失败：" << err;
    }
#endif

private:
    void post(const QByteArray& payload) {
        // 花括号初始化：圆括号写法会被解析成"函数声明"（most vexing parse）
        QNetworkRequest req{QUrl(httpUrl_)};
        req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        auto* reply = nam_.post(req, payload);
        connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
    }

    QString deviceId_;
    QString httpUrl_;
    lx::UdpControllerClient ctrl_;
    QNetworkAccessManager nam_;
    QTimer* publishTimer_ = nullptr;
#ifdef LX_HAVE_MQTT
    QMqttClient* mqtt_ = nullptr;
#endif

    // 窗口统计
    quint32 frameCount_ = 0;
    double forceSum_ = 0.0;
    double forcePeak_ = 0.0;
    double torqueSum_[lx::protocol::kJointCount] = {};
    double tempMax_ = 0.0;
    quint8 lastProgress_ = 0;
    quint8 lastMode_ = 0;
    quint16 lastSeq_ = 0;
    quint32 alarmCount_ = 0;
    quint32 criticalCount_ = 0;
    quint32 produced_ = 0;
    bool finishedLatched_ = false;
    QString lastAlarm_;
};

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("EdgeGateway"));
    QCoreApplication::setApplicationVersion(QStringLiteral(LX_VERSION_STR));
    QCoreApplication::setOrganizationName(QStringLiteral(LX_ORG_NAME));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("灵犀智磨 工业云边网关"));
    parser.addHelpOption();
    parser.addOption({"device-id", QStringLiteral("设备编号"), QStringLiteral("id"),
                      QStringLiteral("LX-0001")});
    parser.addOption({"broker", QStringLiteral("MQTT Broker 地址"), QStringLiteral("host"),
                      QStringLiteral("127.0.0.1")});
    parser.addOption({"port", QStringLiteral("MQTT 端口"), QStringLiteral("port"),
                      QStringLiteral("1883")});
    parser.addOption({"http-url", QStringLiteral("云端 HTTP 上报地址"), QStringLiteral("url"),
                      QString()});
    parser.addOption({"interval", QStringLiteral("上云周期（毫秒）"), QStringLiteral("ms"),
                      QString::number(kDefaultIntervalMs)});
    parser.process(app);

    EdgeGateway gw(parser.value(QStringLiteral("device-id")),
                   parser.value(QStringLiteral("broker")),
                   quint16(parser.value(QStringLiteral("port")).toUInt()),
                   parser.value(QStringLiteral("http-url")),
                   parser.value(QStringLiteral("interval")).toInt());
    return app.exec();
}
