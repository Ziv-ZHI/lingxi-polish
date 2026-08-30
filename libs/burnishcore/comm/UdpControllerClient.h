// 上位机 <-> ARM 控制器 的 UDP 通信客户端
//   - 指令：CmdId + 载荷，发往控制器 kPortCmd（帧格式见 FirmwareProtocol.h）
//   - 遥测：绑定 kPortTele，每 2ms 一帧（UI 侧需自行降采样，避免界面卡顿）
// 注：本类只做通信与解析，不含任何 UI 依赖，工具链/云网关可直接使用
#pragma once

#include "model/AlarmRecord.h"
#include "model/ForceTorqueFrame.h"
#include "model/JointState.h"
#include "model/ProcessRecipe.h"
#include "protocol/FirmwareProtocol.h"

#include <QHostAddress>
#include <QObject>

class QTimer;
class QUdpSocket;

namespace lx {

class UdpControllerClient : public QObject {
    Q_OBJECT
public:
    explicit UdpControllerClient(QObject* parent = nullptr);
    ~UdpControllerClient() override;

    bool bindTelemetry(quint16 port = protocol::kPortTele, QString* err = nullptr);
    void setController(const QHostAddress& addr, quint16 cmdPort = protocol::kPortCmd);

    // —— 指令 ——
    bool sendCommand(protocol::CmdId cmd, const QByteArray& payload = {}, QString* err = nullptr);
    bool setTaskMode(protocol::TaskMode mode, QString* err = nullptr);
    bool setForceTarget(double newtons, double deadbandN, QString* err = nullptr);
    bool uploadAdmittance(const protocol::AdmittanceParams& p, QString* err = nullptr);

    // 从轨迹 CSV（x,y,z,rx,ry,rz 列序，trajectory-planner / 轨迹模块导出）
    // 生成 LoadTrajectory 二进制载荷并下发
    bool loadTrajectoryFile(const QString& csvPath, QString* err = nullptr);

    // 完整工艺下发：导纳参数 -> 目标恒力 -> 轨迹（若配置了轨迹文件）
    // trajBaseDir 用于解析工艺里的相对轨迹路径
    bool uploadRecipe(const ProcessRecipe& r, const QString& trajBaseDir = QString(),
                      QString* err = nullptr);

    // —— 状态 ——
    bool   isOnline() const { return online_; }
    double lastPacketAgeMs() const;
    quint32 droppedFrames() const { return dropped_; }

    // 最近一帧遥测（供标定采集等低频使用；高频请走 telemetryReady 信号）
    bool hasLastFrame() const { return hasFrame_; }
    const protocol::TelemetryFrame& lastFrame() const { return lastFrame_; }

public slots:
    // 本地注入一帧遥测：模拟器 / 数据回放使用，走与网络帧完全相同的分发路径
    void injectFrame(const protocol::TelemetryFrame& frame);

signals:
    void telemetryReady(const protocol::TelemetryFrame& frame);
    void forceSampleReady(const ForceTorqueSample& s);
    void jointSampleReady(const JointStateSample& s);
    void alarmRaised(const AlarmRecord& a);
    void onlineChanged(bool online);

private:
    void onReadyRead();
    void checkTimeout();
    void handleFrame(const protocol::TelemetryFrame& f);
    void raise(AlarmLevel level, AlarmCode code, const QString& text);

    QUdpSocket* cmd_  = nullptr;   // 指令通道（未绑定，直接 writeDatagram）
    QUdpSocket* tele_ = nullptr;   // 遥测通道
    QTimer* watchdog_ = nullptr;   // 通讯超时检测
    QHostAddress ctrlAddr_ = QHostAddress::LocalHost;
    quint16 ctrlCmdPort_ = protocol::kPortCmd;
    bool online_ = false;
    quint16 expectSeq_ = 0;
    quint32 dropped_ = 0;
    qint64  lastPacketMs_ = 0;
    double  t0Us_ = 0.0;

    // 告警边沿触发：控制器每帧都可能带着同一个告警码，不能每帧都报警
    std::uint8_t lastAlarmCode_ = 0;

    protocol::TelemetryFrame lastFrame_;
    bool hasFrame_ = false;
};

}  // namespace lx
