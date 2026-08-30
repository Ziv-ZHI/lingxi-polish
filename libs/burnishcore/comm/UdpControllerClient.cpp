#include "UdpControllerClient.h"

#include <QDataStream>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <QTimer>
#include <QUdpSocket>

#include <cmath>

namespace lx {
namespace {

constexpr int kCommTimeoutMs = 200;   // 超过 200ms 未收到遥测即判定通讯中断

// 指令帧组包：uint32 魔数 | uint16 指令码 | uint32 载荷长度 | 载荷（小端）
QByteArray pack(protocol::CmdId cmd, const QByteArray& payload) {
    QByteArray out;
    QDataStream ds(&out, QIODevice::WriteOnly);
    ds.setByteOrder(QDataStream::LittleEndian);
    ds << protocol::kMagic << static_cast<quint16>(cmd)
       << static_cast<quint32>(payload.size());
    if (!payload.isEmpty()) out.append(payload);
    return out;
}

}  // namespace

UdpControllerClient::UdpControllerClient(QObject* parent) : QObject(parent) {
    cmd_ = new QUdpSocket(this);
    tele_ = new QUdpSocket(this);
    connect(tele_, &QUdpSocket::readyRead, this, &UdpControllerClient::onReadyRead);

    watchdog_ = new QTimer(this);
    connect(watchdog_, &QTimer::timeout, this, &UdpControllerClient::checkTimeout);
    watchdog_->start(100);
}

UdpControllerClient::~UdpControllerClient() = default;

bool UdpControllerClient::bindTelemetry(quint16 port, QString* err) {
    if (!tele_->bind(QHostAddress::AnyIPv4, port, QUdpSocket::ShareAddress)) {
        if (err) *err = QStringLiteral("遥测端口 %1 绑定失败").arg(port);
        return false;
    }
    return true;
}

void UdpControllerClient::setController(const QHostAddress& addr, quint16 cmdPort) {
    ctrlAddr_ = addr;
    ctrlCmdPort_ = cmdPort;
}

bool UdpControllerClient::sendCommand(protocol::CmdId cmd, const QByteArray& payload,
                                      QString* err) {
    const QByteArray frame = pack(cmd, payload);
    const qint64 n = cmd_->writeDatagram(frame, ctrlAddr_, ctrlCmdPort_);
    if (n != frame.size()) {
        if (err) *err = QStringLiteral("指令发送失败（目标 %1:%2）")
                            .arg(ctrlAddr_.toString()).arg(ctrlCmdPort_);
        return false;
    }
    return true;
}

bool UdpControllerClient::setTaskMode(protocol::TaskMode mode, QString* err) {
    QByteArray p;
    p.append(static_cast<char>(mode));
    return sendCommand(protocol::CmdId::SetMode, p, err);
}

bool UdpControllerClient::setForceTarget(double newtons, double deadbandN, QString* err) {
    protocol::ForceTargetPayload p;
    p.targetN = newtons;
    p.deadbandN = deadbandN;
    const QByteArray payload(reinterpret_cast<const char*>(&p), sizeof(p));
    return sendCommand(protocol::CmdId::SetForceTarget, payload, err);
}

bool UdpControllerClient::uploadAdmittance(const protocol::AdmittanceParams& p, QString* err) {
    QByteArray payload(reinterpret_cast<const char*>(&p), sizeof(p));
    return sendCommand(protocol::CmdId::SetAdmittance, payload, err);
}

bool UdpControllerClient::loadTrajectoryFile(const QString& csvPath, QString* err) {
    QFile file(csvPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (err) *err = QStringLiteral("无法打开轨迹文件：%1").arg(csvPath);
        return false;
    }

    // CSV 列序与 trajectory-planner / 轨迹模块导出一致：x,y,z,rx,ry,rz,...
    // 单位 mm / rad，与 FirmwareProtocol.h 的约定相同
    QVector<std::array<double, 6>> points;
    QTextStream in(&file);
    while (!in.atEnd()) {
        const QStringList cols =
            in.readLine().trimmed().split(QLatin1Char(','));
        bool ok = false;
        if (!cols.isEmpty() && !cols.first().isEmpty()) {
            cols.first().toDouble(&ok);   // 表头首列为 index/x 等非数字时跳过
        }
        if (cols.size() < 6 || !ok) continue;
        std::array<double, 6> pt = {};
        for (int i = 0; i < 6; ++i) pt[i] = cols.at(i).toDouble();
        points.append(pt);
    }

    QByteArray payload;
    QDataStream ds(&payload, QIODevice::WriteOnly);
    ds.setByteOrder(QDataStream::LittleEndian);
    ds << static_cast<qint32>(points.size());
    for (const auto& pt : points) {
        for (int i = 0; i < 6; ++i) ds << pt[i];
    }
    return sendCommand(protocol::CmdId::LoadTrajectory, payload, err);
}

bool UdpControllerClient::uploadRecipe(const ProcessRecipe& r, const QString& trajBaseDir,
                                       QString* err) {
    // 下发顺序即固件的生效顺序：先控制律、后目标力、最后轨迹
    if (!uploadAdmittance(r.admittance, err)) return false;
    if (!setForceTarget(r.targetForceN, r.admittance.forceDeadband, err)) return false;
    if (r.trajectoryFile.isEmpty()) return true;   // 纯参数工艺，无轨迹

    QString trajPath = r.trajectoryFile;
    if (QFileInfo(trajPath).isRelative() && !trajBaseDir.isEmpty()) {
        trajPath = QDir(trajBaseDir).filePath(trajPath);
    }
    if (!QFileInfo::exists(trajPath)) {
        if (err) *err = QStringLiteral("轨迹文件不存在：%1").arg(trajPath);
        return false;
    }
    return loadTrajectoryFile(trajPath, err);
}

void UdpControllerClient::injectFrame(const protocol::TelemetryFrame& frame) {
    if (!protocol::isValid(frame)) return;
    handleFrame(frame);
}

double UdpControllerClient::lastPacketAgeMs() const {
    if (lastPacketMs_ == 0) return -1.0;
    return static_cast<double>(QDateTime::currentMSecsSinceEpoch() - lastPacketMs_);
}

void UdpControllerClient::onReadyRead() {
    while (tele_->hasPendingDatagrams()) {
        QByteArray buf;
        buf.resize(static_cast<int>(tele_->pendingDatagramSize()));
        tele_->readDatagram(buf.data(), buf.size());

        // 长度不足/魔数版本不对的帧直接丢弃，防止把脏数据解释成结构体
        if (static_cast<size_t>(buf.size()) < sizeof(protocol::TelemetryFrame)) continue;
        const auto* f = reinterpret_cast<const protocol::TelemetryFrame*>(buf.constData());
        if (!protocol::isValid(*f)) continue;
        handleFrame(*f);
    }
}

void UdpControllerClient::handleFrame(const protocol::TelemetryFrame& f) {
    // 丢包检测：帧序号按 16 位回绕连续计数
    if (expectSeq_ != 0 && f.seq != expectSeq_) dropped_ += 1;
    expectSeq_ = static_cast<quint16>(f.seq + 1);

    lastPacketMs_ = QDateTime::currentMSecsSinceEpoch();
    if (!online_) {
        online_ = true;
        t0Us_ = static_cast<double>(f.timestampUs);
        emit onlineChanged(true);
    }

    lastFrame_ = f;
    hasFrame_ = true;

    // 告警边沿触发：只有告警码发生变化才上报，恢复时补一条提示，
    // 避免控制器持续上报同一告警码时把 UI 弹窗/日志打爆
    if (f.alarmCode != lastAlarmCode_) {
        if (f.alarmCode != 0) {
            raise(AlarmLevel::Critical, static_cast<AlarmCode>(f.alarmCode),
                  QStringLiteral("控制器上报告警码 %1").arg(f.alarmCode));
        } else {
            raise(AlarmLevel::Info, AlarmCode::None,
                  QStringLiteral("控制器告警解除（原告警码 %1）").arg(lastAlarmCode_));
        }
        lastAlarmCode_ = f.alarmCode;
    }

    emit telemetryReady(f);
    emit forceSampleReady(toSample(f, t0Us_));
    emit jointSampleReady(toJointState(f, t0Us_));
}

void UdpControllerClient::checkTimeout() {
    const double age = lastPacketAgeMs();
    if (online_ && age > kCommTimeoutMs) {
        online_ = false;
        lastAlarmCode_ = 0;   // 通讯重建后允许重新上报告警
        raise(AlarmLevel::Critical, AlarmCode::CommLost,
              QStringLiteral("通讯中断：%1 ms 未收到遥测帧").arg(age, 0, 'f', 1));
        emit onlineChanged(false);
    }
}

void UdpControllerClient::raise(AlarmLevel level, AlarmCode code, const QString& text) {
    AlarmRecord a;
    a.time = QDateTime::currentDateTime();
    a.level = level;
    a.code = code;
    a.text = text;
    emit alarmRaised(a);
}

}  // namespace lx
