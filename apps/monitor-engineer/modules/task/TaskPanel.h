// 模块 5：作业执行
//   一键启动/暂停/停止打磨任务，任务进度展示，末端运动实时预览
//   预览视图为轻量投影示意；接入 OpenCASCADE / Qt3D 后可替换为真实三维模型
#pragma once

#include <QVector>
#include <QWidget>

#include "comm/UdpControllerClient.h"

QT_BEGIN_NAMESPACE
class QLabel;
class QProgressBar;
class QPushButton;
QT_END_NAMESPACE

class MotionPreview;

class TaskPanel : public QWidget {
    Q_OBJECT
public:
    explicit TaskPanel(lx::UdpControllerClient* ctrl, QWidget* parent = nullptr);

private slots:
    void onStart();
    void onPause();
    void onStop();
    void onTelemetry(const lx::protocol::TelemetryFrame& f);

private:
    void setRunning(bool running);

    lx::UdpControllerClient* ctrl_ = nullptr;
    MotionPreview* preview_ = nullptr;
    QProgressBar* progress_ = nullptr;
    QLabel* stateLabel_ = nullptr;
    QLabel* forceLabel_ = nullptr;
    QPushButton* startBtn_ = nullptr;
    QPushButton* pauseBtn_ = nullptr;
    QPushButton* stopBtn_ = nullptr;

    bool running_ = false;
    int  frameSkip_ = 0;   // 遥测降采样计数
    int  totalPoints_ = 0;   // 由轨迹模块下发后回填；此处按控制器上报进度更新
};
