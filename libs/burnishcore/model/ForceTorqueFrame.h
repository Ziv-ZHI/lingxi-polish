// 六维力/力矩数据视图：在遥测帧基础上提供合力、分量访问与阈值判定
#pragma once

#include "protocol/FirmwareProtocol.h"

#include <cmath>

namespace lx {

struct ForceTorqueSample {
    double tSec = 0.0;      // 相对时间（秒），供曲线绘制
    double fx = 0.0, fy = 0.0, fz = 0.0;
    double mx = 0.0, my = 0.0, mz = 0.0;

    double norm() const { return std::sqrt(fx * fx + fy * fy + fz * fz); }
    double normTorque() const { return std::sqrt(mx * mx + my * my + mz * mz); }
};

inline ForceTorqueSample toSample(const protocol::TelemetryFrame& f, double t0Us) {
    ForceTorqueSample s;
    s.tSec = (static_cast<double>(f.timestampUs) - t0Us) / 1.0e6;
    s.fx = f.force[0];  s.fy = f.force[1];  s.fz = f.force[2];
    s.mx = f.torque[0]; s.my = f.torque[1]; s.mz = f.torque[2];
    return s;
}

}  // namespace lx
