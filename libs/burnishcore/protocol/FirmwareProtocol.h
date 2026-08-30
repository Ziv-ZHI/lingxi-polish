// =============================================================================
//  与下位机（ARM Cortex 控制器）共享的通信协议定义 —— 唯一协议源
//
//  - 本头文件只依赖 <cstdint>，可被固件侧（linuxUDP.c / Simulink 生成代码）
//    直接包含；上位机与固件必须以同一份文件编译，杜绝两边手抄结构体。
//  - 所有字段小端字节序；结构体 1 字节对齐（#pragma pack(1)）。
//  - 修改任何字段、枚举或载荷格式，必须同步更新固件侧组包/解析，并递增 kVersion。
// =============================================================================
#pragma once

#include <cstdint>

namespace lx {
namespace protocol {

// —— 全局常量 ——
constexpr std::uint32_t kMagic      = 0x4C58504Fu;  // "LXPO"
constexpr std::uint16_t kVersion    = 2;             // 协议版本
constexpr std::uint16_t kPortCmd    = 5001;          // 上位机 -> 控制器（指令）
constexpr std::uint16_t kPortTele   = 5002;          // 控制器 -> 上位机（遥测，2ms 周期）
constexpr int           kJointCount = 6;             // 六轴协作机械臂

// —— 单位约定（上位机 / 固件 / 轨迹文件三方必须一致）——
//   关节角 rad、角速度 rad/s、关节力矩 N·m
//   六维力 N、六维力矩 N·m
//   TCP 位置 mm、TCP 姿态 rad（ZYX 欧拉角 rx/ry/rz）
//   温度 ℃、控制周期 ms、控制器时间戳 us

// 控制器任务模式（与固件状态机一致）
enum class TaskMode : std::uint8_t {
    Free        = 0,  // 自由运动
    Transition  = 1,  // 过渡运动
    Polishing   = 2,  // 打磨受限运动
    Homing      = 3,  // 回零
    Calibration = 4,  // 标定模式
    Estop       = 5   // 急停/故障
};

// 控制模式（EtherCAT 伺服模式，源自固件 Model_type 枚举）
enum class ServoMode : std::uint8_t {
    Csp = 0,  // 同步周期位置
    Cst = 1,  // 同步周期力矩
    Csv = 2   // 同步周期速度
};

// 上位机下发的指令字
enum class CmdId : std::uint16_t {
    SetMode        = 0x0101,
    SetAdmittance  = 0x0102,  // 导纳控制 M/D/K 矩阵
    SetForceTarget = 0x0103,  // 恒力打磨目标力
    LoadTrajectory = 0x0104,
    Start          = 0x0201,
    Pause          = 0x0202,
    Stop           = 0x0203,
    ZeroSensor     = 0x0301,  // 六维力零点标定
    GravityComp    = 0x0302   // 重力补偿参数写入
};

// =============================================================================
//  指令帧统一布局（小端）：uint32 魔数 | uint16 指令码 | uint32 载荷字节数 | 载荷
//  各指令的载荷定义：
//    SetMode        : TaskMode（1 字节）
//    SetAdmittance  : AdmittanceParams（888 字节）
//    SetForceTarget : ForceTargetPayload（16 字节）
//    LoadTrajectory : TrajectoryBlock（4 + 48*n 字节）
//    Start/Pause/Stop/ZeroSensor : 无载荷（0 字节）
//    GravityComp    : GravityCompPayload（56 字节）
// =============================================================================

#pragma pack(push, 1)

// 遥测帧：控制器 -> 上位机（kPortTele，2ms 一帧）
struct TelemetryFrame {
    std::uint32_t magic        = kMagic;
    std::uint16_t version      = kVersion;
    std::uint16_t seq          = 0;      // 帧序号，用于丢包检测（回绕）
    std::uint64_t timestampUs  = 0;      // 控制器时间戳（微秒）
    std::uint8_t  mode         = 0;      // TaskMode
    std::uint8_t  servoMode    = 0;      // ServoMode
    std::uint8_t  alarmCode    = 0;      // 告警码，0 表示无告警（含义见固件告警表）
    std::uint8_t  progress     = 0;      // 任务进度 0~100，由控制器上报
    double jointPos[kJointCount] = {};   // 关节角 rad
    double jointVel[kJointCount] = {};   // 关节角速度 rad/s
    double jointTor[kJointCount] = {};   // 关节力矩 N·m
    double force[3]   = {};              // Fx Fy Fz (N)
    double torque[3]  = {};              // Mx My Mz (N·m)
    double tcpPose[6] = {};              // 末端位姿 x y z (mm) + rx ry rz (rad)
    float  sensorTempC  = 0.0f;          // 六维力传感器温度 ℃
    float  ctrlPeriodMs = 0.0f;          // 实际控制周期 ms
};

// 导纳控制参数：M/D/K 三个 6x6 矩阵按行展开（平动 xyz + 转动 xyz 顺序）
struct AdmittanceParams {
    double mass[36]      = {};
    double damping[36]   = {};
    double stiffness[36] = {};
    double forceTarget   = 0.0;   // 恒力目标 N
    double forceDeadband = 0.0;   // 力死区 N
    double postureGain   = 0.0;   // 姿态自适应增益
};

// SetForceTarget 载荷：不打断打磨的前提下在线改目标力
struct ForceTargetPayload {
    double targetN   = 0.0;
    double deadbandN = 0.0;
};

// LoadTrajectory 载荷：点数 + 逐点 x y z rx ry rz（mm / rad），
// 与上位机轨迹模块、trajectory-planner 导出的 CSV 列序一致
struct TrajectoryBlock {
    std::int32_t count = 0;               // 点数 n，可为 0 表示清除当前轨迹
    // double points[n][6];  // 紧随其后（变长，故不放入定长结构体）
};

// GravityComp 载荷：负载质量 + 力/力矩零偏（标定模块最小二乘结果）
struct GravityCompPayload {
    double massKg      = 0.0;
    double forceBias[3]  = {};
    double torqueBias[3] = {};
};

#pragma pack(pop)

// 帧有效性：UDP 已带校验和，这里只做魔数/版本快速过滤
inline bool isValid(const TelemetryFrame& f) {
    return f.magic == kMagic && f.version == kVersion;
}

// 布局哨兵：任何字段增删都会触发编译错误，防止两边结构悄悄错位
static_assert(sizeof(TelemetryFrame) == 268,   "TelemetryFrame 布局变化，必须同步固件");
static_assert(sizeof(AdmittanceParams) == 888, "AdmittanceParams 布局变化，必须同步固件");
static_assert(sizeof(ForceTargetPayload) == 16, "ForceTargetPayload 布局变化");
static_assert(sizeof(GravityCompPayload) == 56, "GravityCompPayload 布局变化");

}  // namespace protocol
}  // namespace lx
