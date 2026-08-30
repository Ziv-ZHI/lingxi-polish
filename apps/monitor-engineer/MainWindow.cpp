#include "MainWindow.h"

#include <QCheckBox>
#include <QHostAddress>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QStatusBar>
#include <QTabWidget>
#include <QTimer>
#include <QToolBar>

#include "modules/calibration/CalibrationPanel.h"
#include "modules/monitoring/MonitoringPanel.h"
#include "modules/task/TaskPanel.h"
#include "modules/trajectory/TrajectoryPanel.h"
#include "modules/tuning/TuningPanel.h"

MainWindow::MainWindow(lx::UserManager& users, QWidget* parent)
    : QMainWindow(parent), users_(users) {
    if (!requireLogin()) {
        QTimer::singleShot(0, this, &QWidget::close);
        return;
    }

    buildUi();
    buildStatusBar();

    // 遥测模拟器：注入帧与网络帧走完全相同的解析/分发路径，
    // 无真机时即可联调全部界面与告警逻辑（现场请保持关闭）
    sim_ = new lx::TelemetrySimulator(this);
    connect(sim_, &lx::TelemetrySimulator::frameReady,
            &ctrl_, &lx::UdpControllerClient::injectFrame);

    connect(&ctrl_, &lx::UdpControllerClient::alarmRaised,
            this, &MainWindow::onAlarm);
    connect(&ctrl_, &lx::UdpControllerClient::onlineChanged,
            this, &MainWindow::onOnlineChanged);

    setWindowTitle(QStringLiteral("灵犀智磨 打磨机器人监控系统 v%1（工程师版）")
                       .arg(QStringLiteral(LX_VERSION_STR)));
    resize(1440, 960);
}

bool MainWindow::requireLogin() {
    while (!users_.loggedIn()) {
        bool ok = false;
        const QString name = QInputDialog::getText(
            this, QStringLiteral("登录"), QStringLiteral("用户名："),
            QLineEdit::Normal, QString(), &ok);
        if (!ok) return false;

        const QString pwd = QInputDialog::getText(
            this, QStringLiteral("登录"), QStringLiteral("密码："),
            QLineEdit::Password, QString(), &ok);
        if (!ok) return false;

        QString err;
        if (!users_.login(name, pwd, &err)) {
            QMessageBox::critical(this, QStringLiteral("登录失败"), err);
        }
    }
    // 工程师版要求工程师及以上权限（首次部署账号 admin / lingxi@2026）
    if (!users_.canEditParams()) {
        QMessageBox::critical(this, QStringLiteral("权限不足"),
                              QStringLiteral("工程师版上位机需要工程师或管理员权限。"));
        return false;
    }
    return true;
}

void MainWindow::buildUi() {
    // —— 连接工具条：控制器地址 + 模拟数据源开关 ——
    auto* toolbar = addToolBar(QStringLiteral("连接"));
    toolbar->setMovable(false);
    toolbar->addWidget(new QLabel(QStringLiteral("  控制器地址 "), this));
    hostEdit_ = new QLineEdit(QStringLiteral("192.168.1.10"), this);
    hostEdit_->setMaximumWidth(150);
    toolbar->addWidget(hostEdit_);
    auto* applyBtn = new QPushButton(QStringLiteral("应用"), this);
    toolbar->addWidget(applyBtn);
    simCheck_ = new QCheckBox(QStringLiteral("模拟遥测（无控制器开发用）"), this);
    toolbar->addWidget(simCheck_);

    ctrl_.setController(QHostAddress(hostEdit_->text()));
    if (QString err; !ctrl_.bindTelemetry(lx::protocol::kPortTele, &err)) {
        QMessageBox::warning(this, QStringLiteral("通信警告"), err);
    }

    connect(applyBtn, &QPushButton::clicked, this, [this] {
        ctrl_.setController(QHostAddress(hostEdit_->text().trimmed()));
        statusBar()->showMessage(
            QStringLiteral("指令目标已设为 %1").arg(hostEdit_->text().trimmed()), 3000);
    });
    connect(simCheck_, &QCheckBox::toggled, this, [this](bool on) {
        if (on) sim_->start(2);
        else    sim_->stop();   // 停止后 200ms 无遥测，会触发通讯中断告警（符合预期）
    });

    tabs_ = new QTabWidget(this);

    monitoring_  = new MonitoringPanel(&ctrl_, this);
    calibration_ = new CalibrationPanel(&ctrl_, this);
    trajectory_  = new TrajectoryPanel(&ctrl_, this);
    tuning_      = new TuningPanel(&ctrl_, this);
    task_        = new TaskPanel(&ctrl_, this);

    tabs_->addTab(monitoring_,  QStringLiteral("状态监控"));
    tabs_->addTab(calibration_, QStringLiteral("标定工具"));
    tabs_->addTab(trajectory_,  QStringLiteral("轨迹编辑与仿真"));
    tabs_->addTab(tuning_,      QStringLiteral("参数调参"));
    tabs_->addTab(task_,        QStringLiteral("作业执行"));

    setCentralWidget(tabs_);
}

void MainWindow::buildStatusBar() {
    statusOnline_ = new QLabel(QStringLiteral("控制器离线"), this);
    statusBar()->addPermanentWidget(statusOnline_);
    statusBar()->showMessage(QStringLiteral("当前用户：%1（%2）")
                                 .arg(users_.currentUser(),
                                      roleName(users_.currentRole())));
}

void MainWindow::onAlarm(const lx::AlarmRecord& a) {
    // 告警已在上游（客户端）边沿去重，这里只做提示；仅严重告警弹窗
    statusBar()->showMessage(QStringLiteral("[%1] %2")
                                 .arg(a.levelName(), a.text), 5000);
    if (a.level == lx::AlarmLevel::Critical) {
        QMessageBox::critical(this, QStringLiteral("设备告警"), a.text);
    }
}

void MainWindow::onOnlineChanged(bool online) {
    statusOnline_->setText(online ? QStringLiteral("控制器在线")
                                  : QStringLiteral("控制器离线"));
}
