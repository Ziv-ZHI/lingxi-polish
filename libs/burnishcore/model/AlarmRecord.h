// 告警/故障记录：覆盖传感器异常、力矩超限、通讯中断、力超阈值
#pragma once

#include <QDateTime>
#include <QString>
#include <QtGlobal>

namespace lx {

enum class AlarmLevel { Info, Warning, Critical };

enum class AlarmCode : quint8 {
    None            = 0,
    SensorFault     = 1,   // 六维力传感器异常（无数据/校验失败）
    TorqueOverlimit = 2,   // 关节力矩超限
    ForceOverlimit  = 3,   // 打磨力超阈值
    CommLost        = 4,   // 通讯断开（UDP 超时 / EtherCAT 从站掉线）
    OverTemp        = 5,   // 传感器/驱动器过温
    Collision       = 6    // 碰撞检测触发
};

struct AlarmRecord {
    QDateTime   time;
    AlarmLevel  level = AlarmLevel::Info;
    AlarmCode   code  = AlarmCode::None;
    QString     text;

    QString levelName() const {
        switch (level) {
        case AlarmLevel::Critical: return QStringLiteral("严重");
        case AlarmLevel::Warning:  return QStringLiteral("警告");
        default:                   return QStringLiteral("提示");
        }
    }
};

}  // namespace lx
