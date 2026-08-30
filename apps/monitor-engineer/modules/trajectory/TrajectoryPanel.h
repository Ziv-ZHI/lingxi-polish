// 模块 3：轨迹编辑与仿真
//   三种运动模式：自由运动 / 过渡运动 / 打磨受限运动
//   贝塞尔曲线与圆弧插补可视化编辑；导入 STEP 工件模型并取表面点
//   基于 DH 参数离线仿真：关节限位、奇异点（可操作度）、工作台碰撞校验后下发
#pragma once

#include <QVector>
#include <QWidget>

#include "comm/UdpControllerClient.h"

QT_BEGIN_NAMESPACE
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QTableWidget;
QT_END_NAMESPACE

class TrajectoryPanel : public QWidget {
    Q_OBJECT
public:
    explicit TrajectoryPanel(lx::UdpControllerClient* ctrl, QWidget* parent = nullptr);

private:
    struct DhRow {      // 标准 DH 参数
        double a = 0.0;       // 连杆长度 mm
        double alpha = 0.0;   // 连杆扭角 rad
        double d = 0.0;       // 连杆偏距 mm
        double offset = 0.0;  // 关节零位偏移 rad
    };

    struct PathPoint {
        double pose[6] = {};   // x y z (mm) + rx ry rz (rad)
        int motion = 2;        // 0 自由 / 1 过渡 / 2 受限打磨
        int interp = 0;        // 0 直线 / 1 圆弧 / 2 贝塞尔
    };

    void buildUi();
    void onAddPoint();
    void onRemovePoint();
    void onImportStep();
    void onLoadDh();
    void onSimulate();
    void onGenerate();
    void onDownload();

    bool forwardKinematics(const double q[6], double pose[6]) const;
    bool inverseKinematics(const double pose[6], double q[6],
                           const double* seed = nullptr) const;
    double manipulability(const double q[6]) const;
    QVector<PathPoint> interpolate(const QVector<PathPoint>& keys) const;
    bool checkPoint(const double q[6], QString* why) const;
    void logLine(const QString& text);

    lx::UdpControllerClient* ctrl_ = nullptr;

    QVector<DhRow> dh_;
    QVector<PathPoint> keys_;
    QVector<PathPoint> path_;      // 生成的轨迹点

    QComboBox* modeCombo_ = nullptr;
    QComboBox* interpCombo_ = nullptr;
    QTableWidget* pointTable_ = nullptr;
    QTableWidget* logView_ = nullptr;
    QDoubleSpinBox* sampleStep_ = nullptr;
    QDoubleSpinBox* tableZ_ = nullptr;
    QLabel* simSummary_ = nullptr;

    double jointLimit_[6][2] = {};   // 各关节上下限位 rad
    bool  dhLoaded_ = false;
    bool  pathReady_ = false;
};
