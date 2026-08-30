// 模块 2：标定工具
//  相机：内参 / 手眼（eye-in-hand）/ 工作台标定参数管理与 yaml 导出；
//        （实际求解依赖 OpenCV / ROS 标定服务，见 LX_HAVE_OPENCV）
//  六维力：零点标定 / 重力补偿（采集 >=6 组姿态，最小二乘求负载质量与零偏）
#pragma once

#include <QWidget>

#include "comm/UdpControllerClient.h"

#include <QVector>

QT_BEGIN_NAMESPACE
class QComboBox;
class QLabel;
class QPushButton;
class QSpinBox;
class QTableWidget;
QT_END_NAMESPACE

class CalibrationPanel : public QWidget {
    Q_OBJECT
public:
    explicit CalibrationPanel(lx::UdpControllerClient* ctrl, QWidget* parent = nullptr);

private slots:
    void onTypeChanged(int index);
    void onCapture();            // 采集当前姿态下的六维力读数
    void onCompute();            // 执行标定计算 / 下发零点
    void onExport();             // 导出 yaml 参数文件
    void onClear();

private:
    enum class CalibType { CameraIntrinsic, HandEye, WorkTable, ForceZero, GravityComp };

    QWidget* buildCameraPage();
    QWidget* buildForcePage();
    void seedCameraParams();
    QString paramValue(const QString& group, const QString& key) const;
    bool computeGravityComp(QString* result);
    void showResult(const QString& text);

    lx::UdpControllerClient* ctrl_ = nullptr;

    QComboBox* typeCombo_ = nullptr;
    QTableWidget* sampleTable_ = nullptr;
    QTableWidget* camParams_ = nullptr;   // 相机/手眼/工作台参数表
    QPushButton* captureBtn_ = nullptr;
    QPushButton* computeBtn_ = nullptr;
    QPushButton* exportBtn_ = nullptr;
    QLabel* resultLabel_ = nullptr;

    QSpinBox* boardCols_ = nullptr;   // 棋盘格内角点数（列）
    QSpinBox* boardRows_ = nullptr;   // 棋盘格内角点数（行）
    QSpinBox* squareMm_ = nullptr;    // 方格边长 mm

    // 力标定采集：每个姿态下的位姿（rpy, rad）与六维力读数
    struct Pose {
        double rpy[3] = {};
        double force[3] = {};
        double torque[3] = {};
    };
    QVector<Pose> poses_;

    // 最近一次重力补偿标定结果
    struct GravityResult {
        bool valid = false;
        double massKg = 0.0;
        double bias[3] = {};
        double torqueBias[3] = {};
    } gravity_;
};
