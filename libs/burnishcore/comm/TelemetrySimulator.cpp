#include "TelemetrySimulator.h"

#include <QRandomGenerator>
#include <QTimer>

#include <cmath>

namespace lx {
namespace {

constexpr int kStepUs = 2000;        // 与控制器 2ms 周期一致
constexpr double kTaskPeriodS = 30.0; // 模拟任务 30 秒走完一遍进度

constexpr double kPi = 3.14159265358979323846;

}  // namespace

TelemetrySimulator::TelemetrySimulator(QObject* parent) : QObject(parent) {
    timer_ = new QTimer(this);
    timer_->setTimerType(Qt::PreciseTimer);
    connect(timer_, &QTimer::timeout, this, &TelemetrySimulator::onTick);
}

void TelemetrySimulator::start(int periodMs) {
    tUs_ = 0;
    seq_ = 0;
    forceN_ = 6.0;
    timer_->start(qMax(1, periodMs));
}

void TelemetrySimulator::stop() { timer_->stop(); }

bool TelemetrySimulator::isRunning() const { return timer_->isActive(); }

void TelemetrySimulator::onTick() {
    const double t = static_cast<double>(tUs_) / 1.0e6;

    protocol::TelemetryFrame f;   // magic/version 已由默认值填好
    f.seq = ++seq_;
    f.timestampUs = tUs_;
    f.mode = static_cast<std::uint8_t>(protocol::TaskMode::Polishing);
    f.servoMode = static_cast<std::uint8_t>(protocol::ServoMode::Csp);
    f.progress = static_cast<std::uint8_t>(
        std::fmod(t, kTaskPeriodS) / kTaskPeriodS * 100.0);

    // TCP 沿往复扫描线运动，叠加小幅法向起伏，让预览/曲线有真实感
    f.tcpPose[0] = 280.0 + 120.0 * std::sin(0.25 * t);
    f.tcpPose[1] = 60.0 * std::sin(0.6 * t);
    f.tcpPose[2] = 150.0 + 3.0 * std::sin(2.0 * t);
    f.tcpPose[3] = 0.0;
    f.tcpPose[4] = 0.10 * std::sin(1.3 * t);
    f.tcpPose[5] = 0.20 * std::sin(0.5 * t);

    // 恒力闭环示意：一阶收敛到目标 + 打磨纹波 + 测量噪声
    const double ripple = 1.2 * std::sin(2.0 * kPi * 0.8 * t);
    const double noise = (QRandomGenerator::global()->bounded(2001) - 1000) / 1000.0 * 0.15;
    forceN_ += 0.004 * (targetN_ + ripple - forceN_);
    f.force[0] = 0.15 * std::sin(t);
    f.force[1] = 0.10 * std::cos(t);
    f.force[2] = forceN_ + noise;
    f.torque[0] = 0.02 * std::sin(0.9 * t);
    f.torque[1] = 0.02 * std::cos(0.7 * t);
    f.torque[2] = 0.05 * std::sin(0.5 * t);

    // 六个关节做缓慢摆动，与 TCP 运动无严格运动学关系（模拟器仅用于演示）
    for (int i = 0; i < protocol::kJointCount; ++i) {
        const double ph = 0.1 * t + i * 0.7;
        f.jointPos[i] = 0.3 * std::sin(ph) + 0.05 * i;
        f.jointVel[i] = 0.03 * std::cos(ph);
        f.jointTor[i] = 2.0 + 1.5 * std::sin(0.13 * t + i);
    }

    f.sensorTempC = static_cast<float>(41.0 + 0.8 * std::sin(0.05 * t));
    f.ctrlPeriodMs = 2.0f;
    f.alarmCode = 0;

    emit frameReady(f);
    tUs_ += kStepUs;
}

}  // namespace lx
