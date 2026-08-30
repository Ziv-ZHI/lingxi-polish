#include "CalibrationPanel.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

#include <cmath>

#ifdef LX_HAVE_OPENCV
#include <opencv2/calib3d.hpp>
#include <opencv2/core.hpp>
#endif

namespace {
constexpr double kGravity = 9.80665;

// 由 RPY（rad，ZYX 顺序）构造旋转矩阵的第三列，即末端 z 轴在世界系下的方向。
// 重力补偿模型：f_meas = m * g * R[:,2] + b，对 [m, bx, by, bz] 线性，
// 因此可以用普通最小二乘（法方程）求解，无需引入矩阵库。
void zAxisFromRpy(double r, double p, double y, double out[3]) {
    // R = Rz(y) Ry(p) Rx(r) 的第三列为 (sin p cos y, -sin p sin y, cos p)
    out[0] = std::sin(p) * std::cos(y);
    out[1] = -std::sin(p) * std::sin(y);
    out[2] = std::cos(p);
}

// 4 元线性方程组高斯消元（A 为 4x4，b 为长度 4，返回是否可解）
bool solve4(double a[4][4], double b[4], double x[4]) {
    for (int col = 0; col < 4; ++col) {
        int pivot = col;
        for (int r = col + 1; r < 4; ++r) {
            if (std::fabs(a[r][col]) > std::fabs(a[pivot][col])) pivot = r;
        }
        if (std::fabs(a[pivot][col]) < 1e-12) return false;
        if (pivot != col) {
            for (int k = 0; k < 4; ++k) std::swap(a[col][k], a[pivot][k]);
            std::swap(b[col], b[pivot]);
        }
        for (int r = col + 1; r < 4; ++r) {
            const double factor = a[r][col] / a[col][col];
            for (int k = col; k < 4; ++k) a[r][k] -= factor * a[col][k];
            b[r] -= factor * b[col];
        }
    }
    for (int r = 3; r >= 0; --r) {
        double sum = b[r];
        for (int k = r + 1; k < 4; ++k) sum -= a[r][k] * x[k];
        x[r] = sum / a[r][r];
    }
    return true;
}
}  // namespace

CalibrationPanel::CalibrationPanel(lx::UdpControllerClient* ctrl, QWidget* parent)
    : QWidget(parent), ctrl_(ctrl) {
    auto* root = new QVBoxLayout(this);

    auto* head = new QHBoxLayout;
    head->addWidget(new QLabel(QStringLiteral("标定类型"), this));
    typeCombo_ = new QComboBox(this);
    typeCombo_->addItem(QStringLiteral("相机内参标定"), int(CalibType::CameraIntrinsic));
    typeCombo_->addItem(QStringLiteral("手眼标定（eye-in-hand）"), int(CalibType::HandEye));
    typeCombo_->addItem(QStringLiteral("工作台标定"), int(CalibType::WorkTable));
    typeCombo_->addItem(QStringLiteral("六维力零点标定"), int(CalibType::ForceZero));
    typeCombo_->addItem(QStringLiteral("六维力重力补偿"), int(CalibType::GravityComp));
    head->addWidget(typeCombo_, 1);

    captureBtn_ = new QPushButton(QStringLiteral("采集当前姿态"), this);
    computeBtn_ = new QPushButton(QStringLiteral("开始标定"), this);
    exportBtn_  = new QPushButton(QStringLiteral("导出参数文件"), this);
    auto* clearBtn = new QPushButton(QStringLiteral("清空"), this);
    head->addWidget(captureBtn_);
    head->addWidget(computeBtn_);
    head->addWidget(exportBtn_);
    head->addWidget(clearBtn);
    root->addLayout(head);

    auto* body = new QHBoxLayout;
    body->addWidget(buildCameraPage(), 1);
    body->addWidget(buildForcePage(), 1);
    root->addLayout(body, 1);

    resultLabel_ = new QLabel(QStringLiteral("就绪"), this);
    resultLabel_->setWordWrap(true);
    root->addWidget(resultLabel_);

    connect(typeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &CalibrationPanel::onTypeChanged);
    connect(captureBtn_, &QPushButton::clicked, this, &CalibrationPanel::onCapture);
    connect(computeBtn_, &QPushButton::clicked, this, &CalibrationPanel::onCompute);
    connect(exportBtn_, &QPushButton::clicked, this, &CalibrationPanel::onExport);
    connect(clearBtn, &QPushButton::clicked, this, &CalibrationPanel::onClear);

    onTypeChanged(0);
}

QWidget* CalibrationPanel::buildCameraPage() {
    auto* box = new QGroupBox(QStringLiteral("视觉标定参数（内参 / 手眼 / 工作台）"), this);
    auto* v = new QVBoxLayout(box);

    auto* form = new QFormLayout;
    boardCols_ = new QSpinBox(this);
    boardCols_->setRange(3, 20);
    boardCols_->setValue(9);
    boardRows_ = new QSpinBox(this);
    boardRows_->setRange(3, 20);
    boardRows_->setValue(6);
    squareMm_ = new QSpinBox(this);
    squareMm_->setRange(1, 200);
    squareMm_->setValue(25);
    squareMm_->setSuffix(QStringLiteral(" mm"));
    form->addRow(QStringLiteral("棋盘格列向角点数"), boardCols_);
    form->addRow(QStringLiteral("棋盘格行向角点数"), boardRows_);
    form->addRow(QStringLiteral("方格边长"), squareMm_);
    v->addLayout(form);

    camParams_ = new QTableWidget(0, 3, this);
    camParams_->setHorizontalHeaderLabels(
        {QStringLiteral("分组"), QStringLiteral("参数"), QStringLiteral("值")});
    camParams_->horizontalHeader()->setStretchLastSection(true);
    camParams_->setColumnWidth(0, 90);
    camParams_->setColumnWidth(1, 110);
    seedCameraParams();
    v->addWidget(camParams_, 1);
    return box;
}

void CalibrationPanel::seedCameraParams() {
    // 预置常用键位；数值为出厂示例，导入实机标定结果后可直接覆盖保存
    struct Row { const char* group; const char* key; const char* value; };
    const Row rows[] = {
        {"内参", "fx", "1280.0"}, {"内参", "fy", "1280.0"},
        {"内参", "cx", "640.0"},  {"内参", "cy", "512.0"},
        {"内参", "k1", "0.0"},    {"内参", "k2", "0.0"}, {"内参", "k3", "0.0"},
        {"内参", "p1", "0.0"},    {"内参", "p2", "0.0"},
        {"手眼", "h11", "1"}, {"手眼", "h12", "0"}, {"手眼", "h13", "0"}, {"手眼", "h14", "0"},
        {"手眼", "h21", "0"}, {"手眼", "h22", "1"}, {"手眼", "h23", "0"}, {"手眼", "h24", "0"},
        {"手眼", "h31", "0"}, {"手眼", "h32", "0"}, {"手眼", "h33", "1"}, {"手眼", "h34", "0"},
        {"手眼", "h41", "0"}, {"手眼", "h42", "0"}, {"手眼", "h43", "0"}, {"手眼", "h44", "1"},
        {"工作台", "t11", "1"}, {"工作台", "t12", "0"}, {"工作台", "t13", "0"}, {"工作台", "t14", "0"},
        {"工作台", "t21", "0"}, {"工作台", "t22", "1"}, {"工作台", "t23", "0"}, {"工作台", "t24", "0"},
        {"工作台", "t31", "0"}, {"工作台", "t32", "0"}, {"工作台", "t33", "1"}, {"工作台", "t34", "0"},
        {"工作台", "t41", "0"}, {"工作台", "t42", "0"}, {"工作台", "t43", "0"}, {"工作台", "t44", "1"},
    };
    for (const Row& r : rows) {
        const int row = camParams_->rowCount();
        camParams_->insertRow(row);
        auto* g = new QTableWidgetItem(QString::fromUtf8(r.group));
        auto* k = new QTableWidgetItem(QString::fromUtf8(r.key));
        auto* v = new QTableWidgetItem(QString::fromUtf8(r.value));
        g->setFlags(g->flags() & ~Qt::ItemIsEditable);   // 分组/键名只读，值可改
        k->setFlags(k->flags() & ~Qt::ItemIsEditable);
        camParams_->setItem(row, 0, g);
        camParams_->setItem(row, 1, k);
        camParams_->setItem(row, 2, v);
    }
}

QString CalibrationPanel::paramValue(const QString& group, const QString& key) const {
    for (int r = 0; r < camParams_->rowCount(); ++r) {
        const auto* g = camParams_->item(r, 0);
        const auto* k = camParams_->item(r, 1);
        if (g && k && g->text() == group && k->text() == key) {
            const auto* v = camParams_->item(r, 2);
            return v ? v->text() : QString();
        }
    }
    return QString();
}

QWidget* CalibrationPanel::buildForcePage() {
    auto* box = new QGroupBox(QStringLiteral("六维力标定采集（姿态 -> 读数）"), this);
    auto* v = new QVBoxLayout(box);
    sampleTable_ = new QTableWidget(0, 9, this);
    sampleTable_->setHorizontalHeaderLabels(
        {QStringLiteral("Rx"), QStringLiteral("Ry"), QStringLiteral("Rz"),
         QStringLiteral("Fx"), QStringLiteral("Fy"), QStringLiteral("Fz"),
         QStringLiteral("Mx"), QStringLiteral("My"), QStringLiteral("Mz")});
    sampleTable_->horizontalHeader()->setStretchLastSection(true);
    sampleTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    v->addWidget(sampleTable_);
    return box;
}

void CalibrationPanel::onTypeChanged(int index) {
    const bool isForce = index >= int(CalibType::ForceZero);
    captureBtn_->setEnabled(isForce);
    computeBtn_->setEnabled(isForce);
    exportBtn_->setEnabled(true);
}

void CalibrationPanel::onCapture() {
    if (typeCombo_->currentIndex() < int(CalibType::ForceZero)) {
        QMessageBox::information(this, QStringLiteral("提示"),
                                 QStringLiteral("视觉标定通过相机 SDK / ROS 图像话题采集标定板图片，"
                                                "本面板负责参数管理与结果导出。"));
        return;
    }

    // 从最近一帧遥测取真实数据；无控制器时可打开主窗口的“模拟遥测”联调
    if (!ctrl_->hasLastFrame()) {
        QMessageBox::warning(this, QStringLiteral("无遥测数据"),
                             QStringLiteral("尚未收到任何遥测帧，请先连接控制器，"
                                            "或开启主窗口的模拟遥测后再采集。"));
        return;
    }
    const lx::protocol::TelemetryFrame& f = ctrl_->lastFrame();

    Pose p;
    p.rpy[0] = f.tcpPose[3];
    p.rpy[1] = f.tcpPose[4];
    p.rpy[2] = f.tcpPose[5];
    for (int i = 0; i < 3; ++i) {
        p.force[i] = f.force[i];
        p.torque[i] = f.torque[i];
    }
    poses_.push_back(p);

    const int row = sampleTable_->rowCount();
    sampleTable_->insertRow(row);
    for (int c = 0; c < 3; ++c) {
        sampleTable_->setItem(row, c,
            new QTableWidgetItem(QString::number(p.rpy[c], 'f', 4)));
    }
    for (int c = 0; c < 3; ++c) {
        sampleTable_->setItem(row, 3 + c,
            new QTableWidgetItem(QString::number(p.force[c], 'f', 3)));
    }
    for (int c = 0; c < 3; ++c) {
        sampleTable_->setItem(row, 6 + c,
            new QTableWidgetItem(QString::number(p.torque[c], 'f', 4)));
    }
    sampleTable_->scrollToBottom();
    showResult(QStringLiteral("已采集 %1 组姿态（重力补偿至少需要 6 组，"
                              "且各姿态需明显不同）").arg(poses_.size()));
}

bool CalibrationPanel::computeGravityComp(QString* result) {
    if (poses_.size() < 6) {
        *result = QStringLiteral("重力补偿至少需要 6 组不同姿态的采样，当前 %1 组").arg(poses_.size());
        return false;
    }

    // 法方程 A^T A x = A^T b，未知量 x = [m, bx, by, bz]
    double ata[4][4] = {};
    double atb[4] = {};
    for (const Pose& p : poses_) {
        double zAxis[3] = {};
        zAxisFromRpy(p.rpy[0], p.rpy[1], p.rpy[2], zAxis);
        const double rows[3][4] = {{kGravity * zAxis[0], 1.0, 0.0, 0.0},
                                   {kGravity * zAxis[1], 0.0, 1.0, 0.0},
                                   {kGravity * zAxis[2], 0.0, 0.0, 1.0}};
        for (int r = 0; r < 3; ++r) {
            for (int i = 0; i < 4; ++i) {
                atb[i] += rows[r][i] * p.force[r];
                for (int j = 0; j < 4; ++j) ata[i][j] += rows[r][i] * rows[r][j];
            }
        }
        for (int k = 0; k < 3; ++k) gravity_.torqueBias[k] += p.torque[k];
    }

    double x[4] = {};
    if (!solve4(ata, atb, x)) {
        *result = QStringLiteral("姿态样本秩不足，请更换更多不同姿态后重试");
        return false;
    }
    for (int k = 0; k < 3; ++k) gravity_.torqueBias[k] /= poses_.size();

    gravity_.valid = true;
    gravity_.massKg = x[0];
    gravity_.bias[0] = x[1];
    gravity_.bias[1] = x[2];
    gravity_.bias[2] = x[3];

    *result = QStringLiteral("重力补偿完成：负载质量 %1 kg，力零偏 (%2, %3, %4) N，"
                             "力矩零偏 (%5, %6, %7) N·m")
                  .arg(gravity_.massKg, 0, 'f', 3)
                  .arg(gravity_.bias[0], 0, 'f', 3)
                  .arg(gravity_.bias[1], 0, 'f', 3)
                  .arg(gravity_.bias[2], 0, 'f', 3)
                  .arg(gravity_.torqueBias[0], 0, 'f', 4)
                  .arg(gravity_.torqueBias[1], 0, 'f', 4)
                  .arg(gravity_.torqueBias[2], 0, 'f', 4);
    return true;
}

void CalibrationPanel::onCompute() {
    const int idx = typeCombo_->currentIndex();

    if (idx == int(CalibType::ForceZero)) {
        // 零点标定：由固件把当前读数记为零偏，上位机只发指令
        QString err;
        if (!ctrl_->sendCommand(lx::protocol::CmdId::ZeroSensor, {}, &err)) {
            QMessageBox::warning(this, QStringLiteral("下发失败"), err);
            return;
        }
        showResult(QStringLiteral("已下发六维力零点标定指令"));
        return;
    }

    if (idx == int(CalibType::GravityComp)) {
        QString text;
        if (!computeGravityComp(&text)) {
            QMessageBox::warning(this, QStringLiteral("标定失败"), text);
            showResult(text);
            return;
        }
        lx::protocol::GravityCompPayload payload;
        payload.massKg = gravity_.massKg;
        for (int i = 0; i < 3; ++i) {
            payload.forceBias[i] = gravity_.bias[i];
            payload.torqueBias[i] = gravity_.torqueBias[i];
        }
        const QByteArray raw(reinterpret_cast<const char*>(&payload), sizeof(payload));
        QString err;
        if (!ctrl_->sendCommand(lx::protocol::CmdId::GravityComp, raw, &err)) {
            QMessageBox::warning(this, QStringLiteral("下发失败"), err);
        }
        showResult(text);
        return;
    }

#ifdef LX_HAVE_OPENCV
    // 启用 OpenCV 后在此接入 cv::calibrateCamera / calibrateHandEye 的
    // 完整流程（读图 -> 角点 -> 求解 -> 回填参数表）；骨架版本先提示流程
    showResult(QStringLiteral("已启用 OpenCV：请先采集棋盘格图片目录，"
                              "再以 cv::calibrateCamera 求解并回填参数表"));
#else
    QMessageBox::information(this, QStringLiteral("视觉标定"),
        QStringLiteral("视觉标定求解依赖 OpenCV。请在 monitor-engineer.pro 中启用：\n"
                       "DEFINES += LX_HAVE_OPENCV\n"
                       "INCLUDEPATH += <opencv>/build/include\n"
                       "LIBS += -L<opencv>/build/x64/vc16/lib -lopencv_calib3d4xx -lopencv_core4xx\n\n"
                       "未启用时仍可在此管理内参/手眼/工作台参数并导出 yaml。"));
    showResult(QStringLiteral("视觉标定求解未启用（缺少 OpenCV 依赖），参数管理可用"));
#endif
}

void CalibrationPanel::onExport() {
    const int idx = typeCombo_->currentIndex();

    QString text;
    if (idx < int(CalibType::ForceZero)) {
        // 相机类：导出内参/畸变/手眼/工作台 + 棋盘格说明，ROS 侧可直接 rosparam load
        text = QStringLiteral("%YAML:1.0\n---\ncalibType: camera\n"
                              "board:\n  cols: %1\n  rows: %2\n  squareSizeMm: %3\n"
                              "intrinsics:\n")
                   .arg(boardCols_->value()).arg(boardRows_->value()).arg(squareMm_->value());
        const char* keys[9] = {"fx", "fy", "cx", "cy", "k1", "k2", "k3", "p1", "p2"};
        for (const char* k : keys) {
            text += QStringLiteral("  %1: %2\n").arg(QString::fromUtf8(k))
                        .arg(paramValue(QStringLiteral("内参"), QString::fromUtf8(k)));
        }
        text += QStringLiteral("handEyeTransform:\n");
        for (int r = 1; r <= 4; ++r) {
            for (int c = 1; c <= 4; ++c) {
                text += QStringLiteral("  h%1%2: %3\n").arg(r).arg(c)
                            .arg(paramValue(QStringLiteral("手眼"),
                                            QStringLiteral("h%1%2").arg(r).arg(c)));
            }
        }
        text += QStringLiteral("workTableTransform:\n");
        for (int r = 1; r <= 4; ++r) {
            for (int c = 1; c <= 4; ++c) {
                text += QStringLiteral("  t%1%2: %3\n").arg(r).arg(c)
                            .arg(paramValue(QStringLiteral("工作台"),
                                            QStringLiteral("t%1%2").arg(r).arg(c)));
            }
        }
    } else {
        if (!gravity_.valid) {
            QMessageBox::warning(this, QStringLiteral("导出失败"),
                                 QStringLiteral("尚未完成重力补偿标定，请先采集并计算。"));
            return;
        }
        text = QStringLiteral("%YAML:1.0\n---\ncalibType: gravityCompensation\n"
                              "massKg: %1\nforceBias: [%2, %3, %4]\n"
                              "torqueBias: [%5, %6, %7]\n")
                   .arg(gravity_.massKg, 0, 'f', 6)
                   .arg(gravity_.bias[0], 0, 'f', 6)
                   .arg(gravity_.bias[1], 0, 'f', 6)
                   .arg(gravity_.bias[2], 0, 'f', 6)
                   .arg(gravity_.torqueBias[0], 0, 'f', 6)
                   .arg(gravity_.torqueBias[1], 0, 'f', 6)
                   .arg(gravity_.torqueBias[2], 0, 'f', 6);
    }

    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("导出标定参数"),
        QStringLiteral("calib_%1.yaml").arg(typeCombo_->currentIndex()),
        QStringLiteral("YAML 参数 (*.yaml *.yml)"));
    if (path.isEmpty()) return;

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, QStringLiteral("导出失败"), path);
        return;
    }
    f.write(text.toUtf8());
    showResult(QStringLiteral("已导出：%1").arg(path));
}

void CalibrationPanel::onClear() {
    poses_.clear();
    sampleTable_->setRowCount(0);
    gravity_ = GravityResult{};
    showResult(QStringLiteral("已清空采样"));
}

void CalibrationPanel::showResult(const QString& text) { resultLabel_->setText(text); }
