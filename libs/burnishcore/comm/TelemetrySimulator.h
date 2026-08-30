// 无真机时的遥测模拟器：按控制器周期（2ms）生成合成 TelemetryFrame，
// 通过 UdpControllerClient::injectFrame 走与真实网络帧完全相同的处理通路，
// 用于上位机开发、自测与离线演示。数据为合成值，不代表真实设备特性。
#pragma once

#include "protocol/FirmwareProtocol.h"

#include <QObject>

class QTimer;

namespace lx {

class TelemetrySimulator : public QObject {
    Q_OBJECT
public:
    explicit TelemetrySimulator(QObject* parent = nullptr);

    void start(int periodMs = 2);
    void stop();
    bool isRunning() const;

    // 设定演示曲线收敛的恒力目标（N），与调参面板目标力联动更直观
    void setTargetForce(double newtons) { targetN_ = newtons; }

signals:
    void frameReady(const lx::protocol::TelemetryFrame& frame);

private slots:
    void onTick();

private:
    QTimer* timer_ = nullptr;
    std::uint64_t tUs_ = 0;        // 模拟的控制器时间
    std::uint16_t seq_ = 0;
    double targetN_ = 20.0;
    double forceN_ = 6.0;          // 力一阶响应状态量
};

}  // namespace lx
