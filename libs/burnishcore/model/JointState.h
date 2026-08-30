// 关节状态数据视图：角度、转速、力矩，含行程/力矩阈值检查
#pragma once

#include "protocol/FirmwareProtocol.h"

namespace lx {

struct JointStateSample {
    double tSec = 0.0;
    double pos[protocol::kJointCount] = {};  // rad
    double vel[protocol::kJointCount] = {};  // rad/s
    double tor[protocol::kJointCount] = {};  // Nm
};

inline JointStateSample toJointState(const protocol::TelemetryFrame& f, double t0Us) {
    JointStateSample s;
    s.tSec = (static_cast<double>(f.timestampUs) - t0Us) / 1.0e6;
    for (int i = 0; i < protocol::kJointCount; ++i) {
        s.pos[i] = f.jointPos[i];
        s.vel[i] = f.jointVel[i];
        s.tor[i] = f.jointTor[i];
    }
    return s;
}

}  // namespace lx
