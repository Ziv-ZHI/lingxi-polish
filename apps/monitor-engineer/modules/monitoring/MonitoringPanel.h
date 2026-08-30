// 模块 1：硬件状态监控
//  实时显示六维力/力矩、6 关节角/转速/力矩、传感器温度、通讯状态；
//  QtCharts 绘制 力-时间 与 关节位置-时间 曲线（5 秒滑动窗口）；
//  阈值越限告警 -> 告警表 + CSV 落盘。
//  2ms 遥测不直接刷 UI：最新帧缓存后由 25ms 定时器统一降采样刷新。
#pragma once

#include <QWidget>

#include "comm/UdpControllerClient.h"
#include "model/AlarmRecord.h"
#include "util/CsvLogger.h"

#include <QChartGlobal>

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
QT_CHARTS_USE_NAMESPACE            // Qt 5：QtCharts 类位于 QtCharts 命名空间
QT_CHARTS_BEGIN_NAMESPACE
class QChartView;
class QLineSeries;
class QValueAxis;
QT_CHARTS_END_NAMESPACE
#else
// Qt 6：QtCharts 类位于全局命名空间，无需处理命名空间
class QChartView;
class QLineSeries;
class QValueAxis;
#endif

QT_BEGIN_NAMESPACE
class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QTableWidget;
class QTimer;
QT_END_NAMESPACE

class MonitoringPanel : public QWidget {
    Q_OBJECT
public:
    explicit MonitoringPanel(lx::UdpControllerClient* ctrl, QWidget* parent = nullptr);

private slots:
    void onTelemetry(const lx::protocol::TelemetryFrame& f);
    void onForceSample(const lx::ForceTorqueSample& s);
    void onJointSample(const lx::JointStateSample& s);
    void onAlarm(const lx::AlarmRecord& a);
    void onRecordToggled(bool on);
    void refreshUi();   // 25ms 定时降采样刷新，避免 500Hz 数据直接打满 UI

private:
    QWidget* buildValueGrid();
    QWidget* buildJointGrid();
    QChartView* buildForceChart();
    QChartView* buildPositionChart();
    void pushSeries(QLineSeries* s, QValueAxis* axis, double x, double y);
    static void autoScaleY(const QVector<QLineSeries*>& series, QValueAxis* axis);
    void appendAlarmRow(const lx::AlarmRecord& a);
    bool alarmRateLimited();

    lx::UdpControllerClient* ctrl_ = nullptr;
    lx::CsvLogger logger_;

    // 最新数据缓存（500Hz 写入，25Hz 读取）
    lx::protocol::TelemetryFrame lastFrame_;
    lx::ForceTorqueSample lastForce_;
    lx::JointStateSample lastJoint_;
    bool hasForce_ = false;
    bool hasJoint_ = false;

    QLineSeries* seriesFnorm_ = nullptr;
    QLineSeries* seriesFz_ = nullptr;
    QLineSeries* seriesTarget_ = nullptr;
    QValueAxis*  axisForceX_ = nullptr;
    QValueAxis*  axisForceY_ = nullptr;
    QLineSeries* seriesJ_[lx::protocol::kJointCount] = {};
    QValueAxis*  axisPosX_ = nullptr;
    QValueAxis*  axisPosY_ = nullptr;

    QLabel* lblForce_[3] = {nullptr, nullptr, nullptr};
    QLabel* lblTorque_[3] = {nullptr, nullptr, nullptr};
    QLabel* lblJointPos_[lx::protocol::kJointCount] = {};
    QLabel* lblJointVel_[lx::protocol::kJointCount] = {};
    QLabel* lblJointTor_[lx::protocol::kJointCount] = {};
    QLabel* lblTemp_ = nullptr;
    QLabel* lblPeriod_ = nullptr;
    QLabel* lblSeq_ = nullptr;
    QLabel* lblLoss_ = nullptr;

    QTableWidget* alarmTable_ = nullptr;
    QDoubleSpinBox* forceLimit_ = nullptr;
    QDoubleSpinBox* tempLimit_ = nullptr;
    QCheckBox* recordBtn_ = nullptr;
    QTimer* uiTimer_ = nullptr;
    qint64  lastLimitAlarmMs_ = 0;   // 阈值告警限流，防止超限时刷爆告警表
};
