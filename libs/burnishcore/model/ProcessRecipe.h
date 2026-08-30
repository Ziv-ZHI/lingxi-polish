// 打磨工艺配置（工艺卡）：一份配置对应一类工件，可保存为 .json / .ini
// 操作工版只读取，工程师版可编辑并另存
#pragma once

#include "protocol/FirmwareProtocol.h"

#include <QString>
#include <QVector>

namespace lx {

enum class MaterialType { Aluminum, Steel, Wood, Composite, Custom };

enum class ToolType { FlapWheel, SandingBelt, AbrasiveDisc, PolishingPad, Custom };

enum class MotionMode { Free, Transition, Constrained };

struct ProcessRecipe {
    // —— 标识 ——
    QString id;          // 唯一标识，建议：材料-曲面-工具-版本
    QString name;        // 工艺名称
    QString workpiece;   // 工件名 / 对应 STEP 模型文件名
    MaterialType material = MaterialType::Aluminum;
    ToolType     tool     = ToolType::FlapWheel;
    MotionMode   motion   = MotionMode::Constrained;

    // —— 工艺参数 ——
    double targetForceN  = 20.0;   // 恒力打磨目标力
    double feedSpeedMmS  = 15.0;   // 进给速度
    double spindleRpm    = 3000.0; // 打磨头转速
    int    passes        = 2;      // 打磨遍数
    double overlapRatio  = 0.3;    // 轨迹行间重叠率

    // —— 控制参数 ——
    protocol::AdmittanceParams admittance;
    double ctrlPeriodMs = 2.0;     // 控制周期，需与固件一致

    // —— 轨迹 / 标定 ——
    QString trajectoryFile;        // 轨迹点文件（由 trajectory-planner 生成）
    QString handEyeCalibFile;      // 手眼标定 yaml
    QString sensorCalibFile;       // 六维力零点/重力补偿配置

    QString materialName() const;
    QString toolName() const;
    bool    isValid(QString* err = nullptr) const;
};

// 工艺库读写（.json 为主格式；.ini 兼容项目既有 Setting.ini 习惯）
bool saveRecipeJson(const ProcessRecipe& r, const QString& path, QString* err = nullptr);
bool loadRecipeJson(ProcessRecipe& r, const QString& path, QString* err = nullptr);
QVector<ProcessRecipe> loadRecipeLibrary(const QString& dir);

}  // namespace lx
