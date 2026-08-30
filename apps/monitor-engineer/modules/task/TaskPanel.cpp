#include "TaskPanel.h"

#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace {
constexpr int kPreviewPoints = 4000;
constexpr int kPreviewEvery  = 10;   // 2ms -> 20ms 一个预览点，足够流畅且省重绘
}

// —— 末端运动预览：绘制轨迹的 XY 俯视与 XZ 侧视投影 ——
class MotionPreview : public QWidget {
public:
    explicit MotionPreview(QWidget* parent = nullptr) : QWidget(parent) {
        setMinimumHeight(320);
        setAutoFillBackground(true);
        setPalette(QPalette(Qt::white));
    }

    void push(double x, double y, double z) {
        if (track_.size() > kPreviewPoints) track_.pop_front();
        if (side_.size() > kPreviewPoints) side_.pop_front();
        track_.push_back(QPointF(x, y));
        side_.push_back(QPointF(x, z));
        update();
    }

    void clear() { track_.clear(); side_.clear(); update(); }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        // 左：XY 俯视投影；右：XZ 侧视投影
        const QRectF left(10, 10, width() / 2 - 20, height() - 20);
        const QRectF right(width() / 2 + 10, 10, width() / 2 - 20, height() - 20);

        auto drawFrame = [&](const QRectF& r, const QString& title) {
            p.setPen(QPen(Qt::lightGray, 1));
            p.drawRect(r);
            p.drawText(r.topLeft() + QPointF(6, 16), title);
        };
        drawFrame(left, QStringLiteral("XY 俯视"));
        drawFrame(right, QStringLiteral("XZ 侧视"));

        if (track_.size() < 2) return;

        // 以轨迹包围盒自适应缩放（两侧共享 X 范围，保持水平方向一致）
        double minX = 1e9, maxX = -1e9, minY = 1e9, maxY = -1e9;
        for (const QPointF& pt : track_) {
            minX = std::min(minX, pt.x()); maxX = std::max(maxX, pt.x());
            minY = std::min(minY, pt.y()); maxY = std::max(maxY, pt.y());
        }
        for (const QPointF& pt : side_) {
            minY = std::min(minY, pt.y()); maxY = std::max(maxY, pt.y());
        }
        const double spanX = std::max(1.0, maxX - minX);
        const double spanY = std::max(1.0, maxY - minY);

        auto map = [&](double vx, double vy, const QRectF& r) {
            return QPointF(r.left() + (vx - minX) / spanX * r.width(),
                           r.bottom() - (vy - minY) / spanY * r.height());
        };
        auto drawTrack = [&](const QVector<QPointF>& pts, const QRectF& r, const QColor& c) {
            if (pts.size() < 2) return;
            p.setPen(QPen(c, 1.5));
            QPainterPath path;
            path.moveTo(map(pts.first().x(), pts.first().y(), r));
            for (const QPointF& pt : pts) path.lineTo(map(pt.x(), pt.y(), r));
            p.drawPath(path);
        };

        drawTrack(track_, left, QColor(0x2E, 0x7D, 0x32));
        drawTrack(side_, right, QColor(0xC6, 0x28, 0x28));   // XZ 侧视轨迹
    }

private:
    QVector<QPointF> track_;   // XY 俯视
    QVector<QPointF> side_;    // XZ 侧视
};

TaskPanel::TaskPanel(lx::UdpControllerClient* ctrl, QWidget* parent)
    : QWidget(parent), ctrl_(ctrl) {
    auto* root = new QVBoxLayout(this);

    auto* head = new QHBoxLayout;
    startBtn_ = new QPushButton(QStringLiteral("启动"), this);
    pauseBtn_ = new QPushButton(QStringLiteral("暂停"), this);
    stopBtn_  = new QPushButton(QStringLiteral("停止"), this);
    stopBtn_->setObjectName(QStringLiteral("btnDanger"));   // 主题中的红色危险按钮
    pauseBtn_->setEnabled(false);
    stopBtn_->setEnabled(false);
    head->addWidget(startBtn_);
    head->addWidget(pauseBtn_);
    head->addWidget(stopBtn_);
    head->addStretch();
    head->addWidget(new QLabel(QStringLiteral("任务状态"), this));
    stateLabel_ = new QLabel(QStringLiteral("待机"), this);
    head->addWidget(stateLabel_);
    root->addLayout(head);

    progress_ = new QProgressBar(this);
    progress_->setRange(0, 100);
    root->addWidget(progress_);

    forceLabel_ = new QLabel(QStringLiteral("当前打磨力：-- N"), this);
    root->addWidget(forceLabel_);

    auto* box = new QGroupBox(QStringLiteral("末端运动预览"), this);
    auto* v = new QVBoxLayout(box);
    preview_ = new MotionPreview(this);
    v->addWidget(preview_);
    root->addWidget(box, 1);

    connect(startBtn_, &QPushButton::clicked, this, &TaskPanel::onStart);
    connect(pauseBtn_, &QPushButton::clicked, this, &TaskPanel::onPause);
    connect(stopBtn_, &QPushButton::clicked, this, &TaskPanel::onStop);
    connect(ctrl_, &lx::UdpControllerClient::telemetryReady, this, &TaskPanel::onTelemetry);
}

void TaskPanel::setRunning(bool running) {
    running_ = running;
    startBtn_->setEnabled(!running);
    pauseBtn_->setEnabled(running);
    stopBtn_->setEnabled(running);
    if (!running) {
        progress_->setValue(0);
        stateLabel_->setText(QStringLiteral("待机"));
    }
}

void TaskPanel::onStart() {
    QString err;
    if (!ctrl_->setTaskMode(lx::protocol::TaskMode::Polishing, &err) ||
        !ctrl_->sendCommand(lx::protocol::CmdId::Start, {}, &err)) {
        QMessageBox::warning(this, QStringLiteral("启动失败"), err);
        return;
    }
    startBtn_->setText(QStringLiteral("启动"));
    setRunning(true);
    stateLabel_->setText(QStringLiteral("打磨中"));
}

void TaskPanel::onPause() {
    QString err;
    if (!ctrl_->sendCommand(lx::protocol::CmdId::Pause, {}, &err)) {
        QMessageBox::warning(this, QStringLiteral("暂停失败"), err);
        return;
    }
    // 暂停后允许恢复：Start 键重新点亮（再次下发 Start 即恢复），进度不清零
    stateLabel_->setText(QStringLiteral("已暂停"));
    pauseBtn_->setEnabled(false);
    startBtn_->setEnabled(true);
    startBtn_->setText(QStringLiteral("恢复"));
}

void TaskPanel::onStop() {
    QString err;
    if (!ctrl_->sendCommand(lx::protocol::CmdId::Stop, {}, &err)) {
        QMessageBox::warning(this, QStringLiteral("停止失败"), err);
        return;
    }
    startBtn_->setText(QStringLiteral("启动"));
    preview_->clear();
    setRunning(false);
}

void TaskPanel::onTelemetry(const lx::protocol::TelemetryFrame& f) {
    // 2ms 数据同样做 10:1 降采样再刷新控件，避免 500Hz setText 占满 UI 线程
    if (++frameSkip_ % kPreviewEvery != 0) return;

    const double fn = std::sqrt(f.force[0] * f.force[0] + f.force[1] * f.force[1] +
                                f.force[2] * f.force[2]);
    forceLabel_->setText(QStringLiteral("当前打磨力：%1 N").arg(fn, 0, 'f', 2));
    progress_->setValue(f.progress);
    if (running_) preview_->push(f.tcpPose[0], f.tcpPose[1], f.tcpPose[2]);
}
