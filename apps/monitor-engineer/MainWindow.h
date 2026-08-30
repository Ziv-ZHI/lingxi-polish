// 工程师版主窗口：以标签页组装 5 个功能模块，统一持有控制器通信客户端
#pragma once

#include <QMainWindow>

#include "comm/TelemetrySimulator.h"
#include "comm/UdpControllerClient.h"
#include "util/UserManager.h"

class QCheckBox;
class QLabel;
class QLineEdit;
class QTabWidget;

class MonitoringPanel;
class CalibrationPanel;
class TrajectoryPanel;
class TuningPanel;
class TaskPanel;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(lx::UserManager& users, QWidget* parent = nullptr);

private:
    bool requireLogin();
    void buildUi();
    void buildStatusBar();
    void onAlarm(const lx::AlarmRecord& a);
    void onOnlineChanged(bool online);

    lx::UserManager& users_;
    lx::UdpControllerClient ctrl_;
    lx::TelemetrySimulator* sim_ = nullptr;

    QTabWidget* tabs_ = nullptr;
    QLabel* statusOnline_ = nullptr;
    QLineEdit* hostEdit_ = nullptr;
    QCheckBox* simCheck_ = nullptr;

    MonitoringPanel*  monitoring_  = nullptr;
    CalibrationPanel* calibration_ = nullptr;
    TrajectoryPanel*  trajectory_  = nullptr;
    TuningPanel*      tuning_      = nullptr;
    TaskPanel*        task_        = nullptr;
};
