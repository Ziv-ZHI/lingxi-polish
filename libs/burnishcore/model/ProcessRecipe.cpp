#include "ProcessRecipe.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace lx {
namespace {

QString enumKey(MaterialType m) {
    switch (m) {
    case MaterialType::Aluminum:  return QStringLiteral("aluminum");
    case MaterialType::Steel:     return QStringLiteral("steel");
    case MaterialType::Wood:      return QStringLiteral("wood");
    case MaterialType::Composite: return QStringLiteral("composite");
    default:                      return QStringLiteral("custom");
    }
}

MaterialType parseMaterial(const QString& s) {
    if (s == QStringLiteral("aluminum"))  return MaterialType::Aluminum;
    if (s == QStringLiteral("steel"))     return MaterialType::Steel;
    if (s == QStringLiteral("wood"))      return MaterialType::Wood;
    if (s == QStringLiteral("composite")) return MaterialType::Composite;
    return MaterialType::Custom;
}

QString enumKey(ToolType t) {
    switch (t) {
    case ToolType::FlapWheel:    return QStringLiteral("flapWheel");
    case ToolType::SandingBelt:  return QStringLiteral("sandingBelt");
    case ToolType::AbrasiveDisc: return QStringLiteral("abrasiveDisc");
    case ToolType::PolishingPad: return QStringLiteral("polishingPad");
    default:                     return QStringLiteral("custom");
    }
}

ToolType parseTool(const QString& s) {
    if (s == QStringLiteral("flapWheel"))    return ToolType::FlapWheel;
    if (s == QStringLiteral("sandingBelt"))  return ToolType::SandingBelt;
    if (s == QStringLiteral("abrasiveDisc")) return ToolType::AbrasiveDisc;
    if (s == QStringLiteral("polishingPad")) return ToolType::PolishingPad;
    return ToolType::Custom;
}

QString enumKey(MotionMode m) {
    switch (m) {
    case MotionMode::Free:        return QStringLiteral("free");
    case MotionMode::Transition:  return QStringLiteral("transition");
    default:                      return QStringLiteral("constrained");
    }
}

MotionMode parseMotion(const QString& s) {
    if (s == QStringLiteral("free"))       return MotionMode::Free;
    if (s == QStringLiteral("transition")) return MotionMode::Transition;
    return MotionMode::Constrained;
}

QJsonArray matrix36(const double* v) {
    QJsonArray a;
    for (int i = 0; i < 36; ++i) a.append(v[i]);
    return a;
}

void readMatrix36(const QJsonArray& a, double* out) {
    const int n = qMin(a.size(), 36);
    for (int i = 0; i < n; ++i) out[i] = a.at(i).toDouble();
}

}  // namespace

QString ProcessRecipe::materialName() const {
    switch (material) {
    case MaterialType::Aluminum:  return QStringLiteral("铝合金");
    case MaterialType::Steel:     return QStringLiteral("钢材");
    case MaterialType::Wood:      return QStringLiteral("木材");
    case MaterialType::Composite: return QStringLiteral("复合材料");
    default:                      return QStringLiteral("自定义");
    }
}

QString ProcessRecipe::toolName() const {
    switch (tool) {
    case ToolType::FlapWheel:    return QStringLiteral("千叶轮");
    case ToolType::SandingBelt:  return QStringLiteral("砂带");
    case ToolType::AbrasiveDisc: return QStringLiteral("砂盘");
    case ToolType::PolishingPad: return QStringLiteral("抛光垫");
    default:                     return QStringLiteral("自定义");
    }
}

bool ProcessRecipe::isValid(QString* err) const {
    if (id.isEmpty()) {
        if (err) *err = QStringLiteral("工艺编号为空");
        return false;
    }
    if (targetForceN <= 0.0 || targetForceN > 200.0) {
        if (err) *err = QStringLiteral("目标打磨力超出 0~200 N 合理范围");
        return false;
    }
    if (feedSpeedMmS <= 0.0) {
        if (err) *err = QStringLiteral("进给速度必须大于 0");
        return false;
    }
    if (trajectoryFile.isEmpty()) {
        if (err) *err = QStringLiteral("未指定轨迹文件");
        return false;
    }
    return true;
}

bool saveRecipeJson(const ProcessRecipe& r, const QString& path, QString* err) {
    QJsonObject o;
    o[QStringLiteral("schema")]     = QStringLiteral("lingxi.recipe/1");
    o[QStringLiteral("id")]         = r.id;
    o[QStringLiteral("name")]       = r.name;
    o[QStringLiteral("workpiece")]  = r.workpiece;
    o[QStringLiteral("material")]   = enumKey(r.material);
    o[QStringLiteral("tool")]       = enumKey(r.tool);
    o[QStringLiteral("motion")]     = enumKey(r.motion);
    o[QStringLiteral("targetForceN")] = r.targetForceN;
    o[QStringLiteral("feedSpeedMmS")] = r.feedSpeedMmS;
    o[QStringLiteral("spindleRpm")]   = r.spindleRpm;
    o[QStringLiteral("passes")]       = r.passes;
    o[QStringLiteral("overlapRatio")] = r.overlapRatio;
    o[QStringLiteral("ctrlPeriodMs")] = r.ctrlPeriodMs;
    o[QStringLiteral("trajectoryFile")]  = r.trajectoryFile;
    o[QStringLiteral("handEyeCalibFile")] = r.handEyeCalibFile;
    o[QStringLiteral("sensorCalibFile")]  = r.sensorCalibFile;

    QJsonObject adm;
    adm[QStringLiteral("mass")]        = matrix36(r.admittance.mass);
    adm[QStringLiteral("damping")]     = matrix36(r.admittance.damping);
    adm[QStringLiteral("stiffness")]   = matrix36(r.admittance.stiffness);
    adm[QStringLiteral("forceTarget")] = r.admittance.forceTarget;
    adm[QStringLiteral("forceDeadband")] = r.admittance.forceDeadband;
    adm[QStringLiteral("postureGain")] = r.admittance.postureGain;
    o[QStringLiteral("admittance")] = adm;

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (err) *err = QStringLiteral("无法写入文件：%1").arg(path);
        return false;
    }
    f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
    return true;
}

bool loadRecipeJson(ProcessRecipe& r, const QString& path, QString* err) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (err) *err = QStringLiteral("无法打开文件：%1").arg(path);
        return false;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject()) {
        if (err) *err = QStringLiteral("工艺文件格式错误（非 JSON 对象）");
        return false;
    }
    const QJsonObject o = doc.object();
    r.id        = o.value(QStringLiteral("id")).toString();
    r.name      = o.value(QStringLiteral("name")).toString();
    r.workpiece = o.value(QStringLiteral("workpiece")).toString();
    r.material  = parseMaterial(o.value(QStringLiteral("material")).toString());
    r.tool      = parseTool(o.value(QStringLiteral("tool")).toString());
    r.motion    = parseMotion(o.value(QStringLiteral("motion")).toString());
    r.targetForceN  = o.value(QStringLiteral("targetForceN")).toDouble(20.0);
    r.feedSpeedMmS  = o.value(QStringLiteral("feedSpeedMmS")).toDouble(15.0);
    r.spindleRpm    = o.value(QStringLiteral("spindleRpm")).toDouble(3000.0);
    r.passes        = o.value(QStringLiteral("passes")).toInt(2);
    r.overlapRatio  = o.value(QStringLiteral("overlapRatio")).toDouble(0.3);
    r.ctrlPeriodMs  = o.value(QStringLiteral("ctrlPeriodMs")).toDouble(2.0);
    r.trajectoryFile    = o.value(QStringLiteral("trajectoryFile")).toString();
    r.handEyeCalibFile  = o.value(QStringLiteral("handEyeCalibFile")).toString();
    r.sensorCalibFile   = o.value(QStringLiteral("sensorCalibFile")).toString();

    const QJsonObject adm = o.value(QStringLiteral("admittance")).toObject();
    readMatrix36(adm.value(QStringLiteral("mass")).toArray(), r.admittance.mass);
    readMatrix36(adm.value(QStringLiteral("damping")).toArray(), r.admittance.damping);
    readMatrix36(adm.value(QStringLiteral("stiffness")).toArray(), r.admittance.stiffness);
    r.admittance.forceTarget   = adm.value(QStringLiteral("forceTarget")).toDouble(r.targetForceN);
    r.admittance.forceDeadband = adm.value(QStringLiteral("forceDeadband")).toDouble(0.5);
    r.admittance.postureGain   = adm.value(QStringLiteral("postureGain")).toDouble(0.0);
    return true;
}

QVector<ProcessRecipe> loadRecipeLibrary(const QString& dir) {
    QVector<ProcessRecipe> out;
    const QFileInfoList files =
        QDir(dir).entryInfoList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);
    for (const QFileInfo& fi : files) {
        ProcessRecipe r;
        if (loadRecipeJson(r, fi.absoluteFilePath())) out.append(r);
    }
    return out;
}

}  // namespace lx
