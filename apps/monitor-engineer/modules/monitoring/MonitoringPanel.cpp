#include "MonitoringPanel.h"

#include <QCheckBox>
#include <QChart>
#include <QChartView>
#include <QDateTime>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineSeries>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTimer>
#include <QValueAxis>
#include <QVBoxLayout>

#include <limits>

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
using namespace QtCharts;
#endif

namespace {
constexpr int kUiPeriodMs   = 40;   // UI 刷新 25Hz：2ms 遥测降采样 80 倍
constexpr int kWindowPoints = 150;  // 25Hz × 6s ≈ 曲线窗口（横轴按 5s 滑动）
constexpr int kMaxAlarmRows = 500;  // 告警表上限，防止长时间运行撑爆内存
constexpr qint64 kLimitAlarmIntervalMs = 1000;  // 越限告警限流周期
}  // namespace

MonitoringPanel::MonitoringPanel(lx::UdpControllerClient* ctrl, QWidget* parent)
    : QWidget(parent), ctrl_(ctrl) {
    auto* root = new QVBoxLayout(this);
    root->addWidget(buildValueGrid());
    root->addWidget(buildJointGrid());

    auto* charts = new QHBoxLayout;
    charts->addWidget(buildForceChart());
    charts->addWidget(buildPositionChart());
    root->addLayout(charts, 3);

    // —— 告警与操作区 ——
    auto* ops = new QGroupBox(QStringLiteral("告警与记录"), this);
    auto* opsLay = new QHBoxLayout(ops);

    alarmTable_ = new QTableWidget(0, 4, this);
    alarmTable_->setHorizontalHeaderLabels(
        {QStringLiteral("时间"), QStringLiteral("级别"), QStringLiteral("代码"),
         QStringLiteral("描述")});
    alarmTable_->horizontalHeader()->setStretchLastSection(true);
    alarmTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    opsLay->addWidget(alarmTable_, 1);

    auto* side = new QVBoxLayout;
    forceLimit_ = new QDoubleSpinBox(this);
    forceLimit_->setRange(0.0, 300.0);
    forceLimit_->setSingleStep(1.0);
    forceLimit_->setValue(60.0);
    forceLimit_->setSuffix(QStringLiteral(" N"));
    side->addWidget(new QLabel(QStringLiteral("力报警阈值"), this));
    side->addWidget(forceLimit_);

    tempLimit_ = new QDoubleSpinBox(this);
    tempLimit_->setRange(0.0, 150.0);
    tempLimit_->setValue(70.0);
    tempLimit_->setSuffix(QStringLiteral(" ℃"));
    side->addWidget(new QLabel(QStringLiteral("温度报警阈值"), this));
    side->addWidget(tempLimit_);

    recordBtn_ = new QCheckBox(QStringLiteral("记录 CSV 日志"), this);
    side->addWidget(recordBtn_);
    side->addStretch();
    opsLay->addLayout(side);

    root->addWidget(ops, 1);

    // 2ms 数据不直接刷 UI：缓存最新帧，由定时器统一降采样
    uiTimer_ = new QTimer(this);
    uiTimer_->setInterval(kUiPeriodMs);
    connect(uiTimer_, &QTimer::timeout, this, &MonitoringPanel::refreshUi);
    uiTimer_->start();

    connect(ctrl_, &lx::UdpControllerClient::telemetryReady, this, &MonitoringPanel::onTelemetry);
    connect(ctrl_, &lx::UdpControllerClient::forceSampleReady, this, &MonitoringPanel::onForceSample);
    connect(ctrl_, &lx::UdpControllerClient::jointSampleReady, this, &MonitoringPanel::onJointSample);
    connect(ctrl_, &lx::UdpControllerClient::alarmRaised, this, &MonitoringPanel::onAlarm);
    connect(recordBtn_, &QCheckBox::toggled, this, &MonitoringPanel::onRecordToggled);
}

QWidget* MonitoringPanel::buildValueGrid() {
    auto* box = new QGroupBox(QStringLiteral("实时数据"), this);
    auto* g = new QGridLayout(box);

    const char* fNames[3] = {"Fx", "Fy", "Fz"};
    const char* tNames[3] = {"Mx", "My", "Mz"};
    for (int i = 0; i < 3; ++i) {
        g->addWidget(new QLabel(QString::fromUtf8(fNames[i]), this), 0, i * 2);
        lblForce_[i] = new QLabel(QStringLiteral("--"), this);
        g->addWidget(lblForce_[i], 0, i * 2 + 1);

        g->addWidget(new QLabel(QString::fromUtf8(tNames[i]), this), 1, i * 2);
        lblTorque_[i] = new QLabel(QStringLiteral("--"), this);
        g->addWidget(lblTorque_[i], 1, i * 2 + 1);
    }

    lblTemp_ = new QLabel(QStringLiteral("--"), this);
    lblPeriod_ = new QLabel(QStringLiteral("--"), this);
    lblSeq_ = new QLabel(QStringLiteral("--"), this);
    lblLoss_ = new QLabel(QStringLiteral("--"), this);
    g->addWidget(new QLabel(QStringLiteral("传感器温度"), this), 2, 0);
    g->addWidget(lblTemp_, 2, 1);
    g->addWidget(new QLabel(QStringLiteral("控制周期"), this), 2, 2);
    g->addWidget(lblPeriod_, 2, 3);
    g->addWidget(new QLabel(QStringLiteral("帧序号"), this), 2, 4);
    g->addWidget(lblSeq_, 2, 5);
    g->addWidget(new QLabel(QStringLiteral("累计丢帧"), this), 2, 6);
    g->addWidget(lblLoss_, 2, 7);
    return box;
}

QWidget* MonitoringPanel::buildJointGrid() {
    auto* box = new QGroupBox(QStringLiteral("六关节状态（角度 / 转速 / 力矩）"), this);
    auto* g = new QGridLayout(box);
    const char* heads[3] = {"角度 rad", "角速度 rad/s", "力矩 N·m"};
    for (int c = 0; c < 3; ++c) {
        g->addWidget(new QLabel(QString::fromUtf8(heads[c]), this), 0, c + 1);
    }
    for (int j = 0; j < lx::protocol::kJointCount; ++j) {
        g->addWidget(new QLabel(QStringLiteral("J%1").arg(j + 1), this), j + 1, 0);
        lblJointPos_[j] = new QLabel(QStringLiteral("--"), this);
        lblJointVel_[j] = new QLabel(QStringLiteral("--"), this);
        lblJointTor_[j] = new QLabel(QStringLiteral("--"), this);
        g->addWidget(lblJointPos_[j], j + 1, 1);
        g->addWidget(lblJointVel_[j], j + 1, 2);
        g->addWidget(lblJointTor_[j], j + 1, 3);
    }
    return box;
}

QChartView* MonitoringPanel::buildForceChart() {
    seriesFnorm_ = new QLineSeries(this);
    seriesFz_ = new QLineSeries(this);
    seriesTarget_ = new QLineSeries(this);
    seriesFnorm_->setName(QStringLiteral("合力 |F|"));
    seriesFz_->setName(QStringLiteral("Fz"));
    seriesTarget_->setName(QStringLiteral("阈值"));

    auto* chart = new QChart;
    chart->addSeries(seriesFnorm_);
    chart->addSeries(seriesFz_);
    chart->addSeries(seriesTarget_);
    chart->setTitle(QStringLiteral("力-时间（5s 窗口）"));
    chart->legend()->setAlignment(Qt::AlignBottom);
    chart->createDefaultAxes();
    axisForceX_ = qobject_cast<QValueAxis*>(chart->axes(Qt::Horizontal).first());
    axisForceX_->setTitleText(QStringLiteral("t / s"));
    axisForceY_ = qobject_cast<QValueAxis*>(chart->axes(Qt::Vertical).first());
    axisForceY_->setTitleText(QStringLiteral("F / N"));

    auto* view = new QChartView(chart, this);
    view->setRenderHint(QPainter::Antialiasing);
    return view;
}

QChartView* MonitoringPanel::buildPositionChart() {
    auto* chart = new QChart;
    for (int i = 0; i < lx::protocol::kJointCount; ++i) {
        seriesJ_[i] = new QLineSeries(this);
        seriesJ_[i]->setName(QStringLiteral("J%1").arg(i + 1));
        chart->addSeries(seriesJ_[i]);
    }
    chart->setTitle(QStringLiteral("关节位置-时间（5s 窗口）"));
    chart->legend()->setAlignment(Qt::AlignBottom);
    chart->createDefaultAxes();
    axisPosX_ = qobject_cast<QValueAxis*>(chart->axes(Qt::Horizontal).first());
    axisPosX_->setTitleText(QStringLiteral("t / s"));
    axisPosY_ = qobject_cast<QValueAxis*>(chart->axes(Qt::Vertical).first());
    axisPosY_->setTitleText(QStringLiteral("rad"));
    auto* view = new QChartView(chart, this);
    view->setRenderHint(QPainter::Antialiasing);
    return view;
}

void MonitoringPanel::pushSeries(QLineSeries* s, QValueAxis* axis, double x, double y) {
    s->append(x, y);
    if (s->count() > kWindowPoints) s->removePoints(0, s->count() - kWindowPoints);
    if (axis && x > 5.0) axis->setRange(x - 5.0, x + 0.5);   // 5 秒滑动窗口
}

void MonitoringPanel::autoScaleY(const QVector<QLineSeries*>& series, QValueAxis* axis) {
    // Qt Charts 的 Y 轴不会随 append 自动扩量程，需按窗口内数据手动适配，
    // 否则合力 ~20N 的曲线会被卡在默认 0~1 轴之外
    double lo = std::numeric_limits<double>::max();
    double hi = -lo;
    for (const QLineSeries* s : series) {
        const auto pts = s->points();
        for (const QPointF& p : pts) {
            lo = std::min(lo, p.y());
            hi = std::max(hi, p.y());
        }
    }
    if (hi <= lo) hi = lo + 1.0;
    const double m = (hi - lo) * 0.1;
    axis->setRange(lo - m, hi + m);
}

bool MonitoringPanel::alarmRateLimited() {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - lastLimitAlarmMs_ < kLimitAlarmIntervalMs) return true;
    lastLimitAlarmMs_ = now;
    return false;
}

void MonitoringPanel::onTelemetry(const lx::protocol::TelemetryFrame& f) {
    lastFrame_ = f;
    logger_.append(f);   // CsvLogger 内部自行抽稀，与 UI 刷新率解耦
}

void MonitoringPanel::onForceSample(const lx::ForceTorqueSample& s) {
    lastForce_ = s;
    hasForce_ = true;

    // 越限判定按全速做（不能漏检），报警展示按 1s 限流
    if (s.norm() > forceLimit_->value() && !alarmRateLimited()) {
        lx::AlarmRecord a;
        a.time = QDateTime::currentDateTime();
        a.level = lx::AlarmLevel::Warning;
        a.code = lx::AlarmCode::ForceOverlimit;
        a.text = QStringLiteral("打磨合力 %1 N 超过阈值 %2 N")
                     .arg(s.norm(), 0, 'f', 1).arg(forceLimit_->value(), 0, 'f', 1);
        onAlarm(a);
    }
}

void MonitoringPanel::onJointSample(const lx::JointStateSample& s) {
    lastJoint_ = s;
    hasJoint_ = true;
}

void MonitoringPanel::refreshUi() {
    if (!hasForce_) return;

    const double fv[3] = {lastForce_.fx, lastForce_.fy, lastForce_.fz};
    const double mv[3] = {lastForce_.mx, lastForce_.my, lastForce_.mz};
    for (int i = 0; i < 3; ++i) {
        lblForce_[i]->setText(QStringLiteral("%1 N").arg(fv[i], 0, 'f', 2));
        lblTorque_[i]->setText(QStringLiteral("%1 N·m").arg(mv[i], 0, 'f', 3));
    }
    pushSeries(seriesFnorm_, axisForceX_, lastForce_.tSec, lastForce_.norm());
    pushSeries(seriesFz_, axisForceX_, lastForce_.tSec, lastForce_.fz);
    pushSeries(seriesTarget_, axisForceX_, lastForce_.tSec, forceLimit_->value());
    autoScaleY({seriesFnorm_, seriesFz_, seriesTarget_}, axisForceY_);

    if (hasJoint_) {
        for (int j = 0; j < lx::protocol::kJointCount; ++j) {
            lblJointPos_[j]->setText(QString::number(lastJoint_.pos[j], 'f', 3));
            lblJointVel_[j]->setText(QString::number(lastJoint_.vel[j], 'f', 3));
            lblJointTor_[j]->setText(QString::number(lastJoint_.tor[j], 'f', 2));
            pushSeries(seriesJ_[j], axisPosX_, lastJoint_.tSec, lastJoint_.pos[j]);
        }
        QVector<QLineSeries*> js;
        for (int j = 0; j < lx::protocol::kJointCount; ++j) js.append(seriesJ_[j]);
        autoScaleY(js, axisPosY_);
    }

    lblTemp_->setText(QStringLiteral("%1 ℃").arg(lastFrame_.sensorTempC, 0, 'f', 1));
    lblPeriod_->setText(QStringLiteral("%1 ms").arg(lastFrame_.ctrlPeriodMs, 0, 'f', 2));
    lblSeq_->setText(QString::number(lastFrame_.seq));
    lblLoss_->setText(QString::number(ctrl_->droppedFrames()));

    if (lastFrame_.sensorTempC > tempLimit_->value() && !alarmRateLimited()) {
        lx::AlarmRecord a;
        a.time = QDateTime::currentDateTime();
        a.level = lx::AlarmLevel::Warning;
        a.code = lx::AlarmCode::OverTemp;
        a.text = QStringLiteral("传感器温度 %1 ℃ 超过阈值 %2 ℃")
                     .arg(lastFrame_.sensorTempC, 0, 'f', 1).arg(tempLimit_->value(), 0, 'f', 0);
        onAlarm(a);
    }
}

void MonitoringPanel::appendAlarmRow(const lx::AlarmRecord& a) {
    const int row = alarmTable_->rowCount();
    alarmTable_->insertRow(row);
    alarmTable_->setItem(row, 0, new QTableWidgetItem(a.time.toString(QStringLiteral("HH:mm:ss"))));
    alarmTable_->setItem(row, 1, new QTableWidgetItem(a.levelName()));
    alarmTable_->setItem(row, 2, new QTableWidgetItem(QString::number(static_cast<int>(a.code))));
    alarmTable_->setItem(row, 3, new QTableWidgetItem(a.text));
    if (alarmTable_->rowCount() > kMaxAlarmRows) {
        alarmTable_->removeRow(0);   // 滚动保留最近告警
    }
    alarmTable_->scrollToBottom();
}

void MonitoringPanel::onAlarm(const lx::AlarmRecord& a) { appendAlarmRow(a); }

void MonitoringPanel::onRecordToggled(bool on) {
    if (on) {
        const QString path = QFileDialog::getSaveFileName(
            this, QStringLiteral("保存运行日志"),
            QStringLiteral("polish_log_%1.csv")
                .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"))),
            QStringLiteral("CSV 数据 (*.csv)"));
        if (path.isEmpty()) {
            recordBtn_->setChecked(false);
            return;
        }
        QString err;
        if (!logger_.start(path, 5, &err)) {   // 每 5 帧存 1 条 = 10ms 分辨率
            QMessageBox::warning(this, QStringLiteral("记录失败"), err);
            recordBtn_->setChecked(false);
            return;
        }
    } else {
        logger_.stop();
    }
}
