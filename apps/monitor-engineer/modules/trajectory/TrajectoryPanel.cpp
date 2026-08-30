#include "TrajectoryPanel.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include <cmath>

#ifdef LX_HAVE_OCC
#include <STEPControl_Reader.hxx>
#include <TopExp_Explorer.hxx>
#endif

namespace {
#ifndef M_PI
#define M_PI 3.14159265358979323846   // MSVC 未定义 _USE_MATH_DEFINES 时的兜底
#endif

// 六轴协作臂行程（rad），实机以出厂标定为准，可在 DH 文件中带 limits 字段覆盖
constexpr double kJointLimitDefault[6][2] = {
    {-3.14, 3.14}, {-2.36, 2.36}, {-2.97, 2.97},
    {-3.14, 3.14}, {-2.36, 2.36}, {-3.14, 3.14}};

// 内置示例 DH（单位 mm / rad）：UR 类协作臂构型。
// 仅为让离线仿真开箱即用，实机必须通过"载入 DH 参数"替换为出厂标定表！
constexpr double kPiHalf = 1.5707963267948966;
constexpr double kDefaultDh[6][4] = {
    {0.0,     kPiHalf,  162.5, 0.0},
    {-425.0,  0.0,      0.0,   0.0},
    {-392.2,  0.0,      0.0,   0.0},
    {0.0,     kPiHalf,  133.3, 0.0},
    {0.0,     -kPiHalf, 416.4, 0.0},
    {0.0,     0.0,      159.1, 0.0}};
constexpr double kManipulabilityMin = 1.0e2;   // sqrt(det(J·J^T)) 下限（mm^3/s，仿真用经验值）

// 6x6 线性方程组求解（部分选主元高斯消元）
bool solve6(double a[6][6], double b[6], double x[6]) {
    for (int col = 0; col < 6; ++col) {
        int pivot = col;
        for (int r = col + 1; r < 6; ++r) {
            if (std::fabs(a[r][col]) > std::fabs(a[pivot][col])) pivot = r;
        }
        if (std::fabs(a[pivot][col]) < 1e-12) return false;
        if (pivot != col) {
            for (int k = 0; k < 6; ++k) std::swap(a[col][k], a[pivot][k]);
            std::swap(b[col], b[pivot]);
        }
        for (int r = col + 1; r < 6; ++r) {
            const double f = a[r][col] / a[col][col];
            for (int k = col; k < 6; ++k) a[r][k] -= f * a[col][k];
            b[r] -= f * b[col];
        }
    }
    for (int r = 5; r >= 0; --r) {
        double s = b[r];
        for (int k = r + 1; k < 6; ++k) s -= a[r][k] * x[k];
        x[r] = s / a[r][r];
    }
    return true;
}

// 三次贝塞尔求值
double bezier(double p0, double p1, double p2, double p3, double t) {
    const double u = 1.0 - t;
    return u * u * u * p0 + 3.0 * u * u * t * p1 + 3.0 * u * t * t * p2 + t * t * t * p3;
}
}  // namespace

TrajectoryPanel::TrajectoryPanel(lx::UdpControllerClient* ctrl, QWidget* parent)
    : QWidget(parent), ctrl_(ctrl) {
    for (int i = 0; i < 6; ++i) {
        jointLimit_[i][0] = kJointLimitDefault[i][0];
        jointLimit_[i][1] = kJointLimitDefault[i][1];
        dh_.push_back(DhRow{kDefaultDh[i][0], kDefaultDh[i][1],
                            kDefaultDh[i][2], kDefaultDh[i][3]});
    }
    dhLoaded_ = true;   // 内置示例参数；载入实机 DH 后覆盖
    buildUi();
}

void TrajectoryPanel::buildUi() {
    auto* root = new QHBoxLayout(this);

    // —— 左侧：关键点编辑 ——
    auto* left = new QVBoxLayout;

    auto* cfg = new QGroupBox(QStringLiteral("运动与插补"), this);
    auto* cfgLay = new QFormLayout(cfg);
    modeCombo_ = new QComboBox(this);
    modeCombo_->addItem(QStringLiteral("自由运动"));
    modeCombo_->addItem(QStringLiteral("过渡运动"));
    modeCombo_->addItem(QStringLiteral("打磨受限运动"));
    modeCombo_->setCurrentIndex(2);
    interpCombo_ = new QComboBox(this);
    interpCombo_->addItem(QStringLiteral("直线插补"));
    interpCombo_->addItem(QStringLiteral("圆弧插补"));
    interpCombo_->addItem(QStringLiteral("贝塞尔曲线"));
    sampleStep_ = new QDoubleSpinBox(this);
    sampleStep_->setRange(0.1, 20.0);
    sampleStep_->setValue(1.0);
    sampleStep_->setSuffix(QStringLiteral(" mm"));
    tableZ_ = new QDoubleSpinBox(this);
    tableZ_->setRange(-2000.0, 2000.0);
    tableZ_->setValue(0.0);
    tableZ_->setSuffix(QStringLiteral(" mm"));
    cfgLay->addRow(QStringLiteral("运动模式"), modeCombo_);
    cfgLay->addRow(QStringLiteral("插补方式"), interpCombo_);
    cfgLay->addRow(QStringLiteral("插补步长"), sampleStep_);
    cfgLay->addRow(QStringLiteral("工作台高度 Z"), tableZ_);
    left->addWidget(cfg);

    pointTable_ = new QTableWidget(0, 6, this);
    pointTable_->setHorizontalHeaderLabels(
        {QStringLiteral("X"), QStringLiteral("Y"), QStringLiteral("Z"),
         QStringLiteral("Rx"), QStringLiteral("Ry"), QStringLiteral("Rz")});
    pointTable_->horizontalHeader()->setStretchLastSection(true);
    left->addWidget(pointTable_, 1);

    auto* btns = new QHBoxLayout;
    auto* addBtn = new QPushButton(QStringLiteral("添加关键点"), this);
    auto* delBtn = new QPushButton(QStringLiteral("删除"), this);
    auto* stepBtn = new QPushButton(QStringLiteral("导入 STEP 取点"), this);
    auto* dhBtn = new QPushButton(QStringLiteral("载入 DH 参数"), this);
    btns->addWidget(addBtn);
    btns->addWidget(delBtn);
    btns->addWidget(stepBtn);
    btns->addWidget(dhBtn);
    left->addLayout(btns);
    root->addLayout(left, 2);

    // —— 右侧：仿真与下发 ——
    auto* right = new QVBoxLayout;
    simSummary_ = new QLabel(QStringLiteral("尚未仿真"), this);
    simSummary_->setWordWrap(true);
    right->addWidget(simSummary_);

    logView_ = new QTableWidget(0, 1, this);
    logView_->setHorizontalHeaderLabels({QStringLiteral("仿真日志")});
    logView_->horizontalHeader()->setStretchLastSection(true);
    logView_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    right->addWidget(logView_, 1);

    auto* run = new QHBoxLayout;
    auto* simBtn = new QPushButton(QStringLiteral("离线仿真"), this);
    auto* genBtn = new QPushButton(QStringLiteral("生成轨迹"), this);
    auto* downBtn = new QPushButton(QStringLiteral("下发机器人"), this);
    run->addWidget(simBtn);
    run->addWidget(genBtn);
    run->addWidget(downBtn);
    right->addLayout(run);
    root->addLayout(right, 1);

    connect(addBtn, &QPushButton::clicked, this, &TrajectoryPanel::onAddPoint);
    connect(delBtn, &QPushButton::clicked, this, &TrajectoryPanel::onRemovePoint);
    connect(stepBtn, &QPushButton::clicked, this, &TrajectoryPanel::onImportStep);
    connect(dhBtn, &QPushButton::clicked, this, &TrajectoryPanel::onLoadDh);
    connect(simBtn, &QPushButton::clicked, this, &TrajectoryPanel::onSimulate);
    connect(genBtn, &QPushButton::clicked, this, &TrajectoryPanel::onGenerate);
    connect(downBtn, &QPushButton::clicked, this, &TrajectoryPanel::onDownload);
}

bool TrajectoryPanel::forwardKinematics(const double q[6], double pose[6]) const {
    // 标准 DH：T = Rz(theta) Tz(d) Tx(a) Rx(alpha) 连乘
    double T[4][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};
    for (int i = 0; i < 6; ++i) {
        const double th = q[i] + dh_.at(i).offset;
        const double ct = std::cos(th), st = std::sin(th);
        const double ca = std::cos(dh_.at(i).alpha), sa = std::sin(dh_.at(i).alpha);
        const double a = dh_.at(i).a, d = dh_.at(i).d;

        const double Ai[4][4] = {
            {ct, -st * ca,  st * sa, a * ct},
            {st,  ct * ca, -ct * sa, a * st},
            {0.0,      sa,       ca,      d},
            {0.0,     0.0,      0.0,    1.0}};

        double R[4][4] = {};
        for (int r = 0; r < 4; ++r) {
            for (int c = 0; c < 4; ++c) {
                double sum = 0.0;
                for (int k = 0; k < 4; ++k) sum += T[r][k] * Ai[k][c];
                R[r][c] = sum;
            }
        }
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c) T[r][c] = R[r][c];
    }
    pose[0] = T[0][3];
    pose[1] = T[1][3];
    pose[2] = T[2][3];
    pose[3] = std::atan2(T[2][1], T[2][2]);
    pose[4] = std::asin(std::max(-1.0, std::min(1.0, -T[2][0])));
    pose[5] = std::atan2(T[1][0], T[0][0]);
    return true;
}

double TrajectoryPanel::manipulability(const double q[6]) const {
    // 位置雅可比（3x6）数值差分，取 sqrt(det(J·J^T)) 作为可操作度指标
    const double h = 1.0e-4;
    double base[6] = {};
    forwardKinematics(q, base);

    double Jp[3][6];
    for (int j = 0; j < 6; ++j) {
        double qp[6];
        for (int k = 0; k < 6; ++k) qp[k] = q[k];
        qp[j] += h;
        double pp[6] = {};
        forwardKinematics(qp, pp);
        for (int r = 0; r < 3; ++r) Jp[r][j] = (pp[r] - base[r]) / h;
    }

    double m[3][3] = {};
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c)
            for (int k = 0; k < 6; ++k) m[r][c] += Jp[r][k] * Jp[c][k];

    const double det = m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1])
                     - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0])
                     + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
    return std::sqrt(std::fabs(det));
}

bool TrajectoryPanel::inverseKinematics(const double pose[6], double q[6],
                                        const double* seed) const {
    // 阻尼最小二乘（DLS）数值迭代：dq = J^T (J J^T + lambda^2 I)^-1 e
    // 要点（均为数值实验验证过的结论）：
    //  1) J1 初值指向目标方位，且示例臂 a2+a3<0（零位伸向 -x），需再 +π；
    //  2) 姿态误差按 2π 归一化，否则欧拉角差会驱动关节空转绕圈；
    //  3) 收敛判据位置 0.05mm + 姿态 0.01rad 双阈值；
    //  4) 收敛解逐关节 wrap 回 [-π,π]（FK 不变），再交给限位/奇异/碰撞检查；
    //  5) 轨迹校验时传入上一点解作为 seed（warm start），保证整条轨迹
    //     解分支连续——独立求 IK 会在分支间跳变，实机上等于关节飞转。
    constexpr double lambda = 0.02;
    const double sum23 = dh_.at(1).a + dh_.at(2).a;
    const double j1Base = std::atan2(pose[1], pose[0]) + (sum23 < 0.0 ? M_PI : 0.0);
    const double seeds[3][2] = {{0.0, 0.0}, {-0.6, 0.9}, {0.6, -0.9}};   // (J2, J3)
    const int seedCount = seed ? 1 : 3;
    auto wrapPi = [](double a) {
        while (a > M_PI) a -= 2.0 * M_PI;
        while (a < -M_PI) a += 2.0 * M_PI;
        return a;
    };

    for (int si = 0; si < seedCount; ++si) {
        if (seed) {
            for (int i = 0; i < 6; ++i) q[i] = seed[i];
        } else {
            for (int i = 0; i < 6; ++i) q[i] = 0.0;
            q[0] = j1Base;
            q[1] = seeds[si][0];
            q[2] = seeds[si][1];
        }

        for (int iter = 0; iter < 200; ++iter) {
            double cur[6] = {};
            forwardKinematics(q, cur);

            double e[6];
            for (int i = 0; i < 3; ++i) e[i] = pose[i] - cur[i];
            for (int i = 3; i < 6; ++i) e[i] = wrapPi(pose[i] - cur[i]);
            double maxPos = 0.0, maxRot = 0.0;
            for (int i = 0; i < 3; ++i) maxPos = std::max(maxPos, std::fabs(e[i]));
            for (int i = 3; i < 6; ++i) maxRot = std::max(maxRot, std::fabs(e[i]));
            if (maxPos < 0.05 && maxRot < 0.01) {
                for (int i = 0; i < 6; ++i) q[i] = wrapPi(q[i]);
                return true;
            }

            const double h = 1.0e-4;
            double J[6][6];
            for (int j = 0; j < 6; ++j) {
                double qp[6];
                for (int k = 0; k < 6; ++k) qp[k] = q[k];
                qp[j] += h;
                double pp[6] = {};
                forwardKinematics(qp, pp);
                for (int r = 0; r < 6; ++r) J[r][j] = (pp[r] - cur[r]) / h;
            }

            double a[6][6] = {};
            for (int r = 0; r < 6; ++r) {
                for (int c = 0; c < 6; ++c) {
                    double sum = 0.0;
                    for (int k = 0; k < 6; ++k) sum += J[r][k] * J[c][k];
                    a[r][c] = sum + (r == c ? lambda * lambda : 0.0);
                }
            }
            double y[6] = {};
            if (!solve6(a, e, y)) break;   // 此起点奇异，换下一个起点
            for (int i = 0; i < 6; ++i) {
                double dq = 0.0;
                for (int r = 0; r < 6; ++r) dq += J[r][i] * y[r];
                q[i] += dq;
            }
        }
    }
    return false;
}

bool TrajectoryPanel::checkPoint(const double q[6], QString* why) const {
    for (int i = 0; i < 6; ++i) {
        if (q[i] < jointLimit_[i][0] || q[i] > jointLimit_[i][1]) {
            *why = QStringLiteral("关节 J%1 超出限位（%2 rad）").arg(i + 1).arg(q[i], 0, 'f', 3);
            return false;
        }
    }
    const double w = manipulability(q);
    if (w < kManipulabilityMin) {
        *why = QStringLiteral("接近奇异位形（可操作度 %1）").arg(w, 0, 'e', 2);
        return false;
    }
    double pose[6] = {};
    forwardKinematics(q, pose);
    if (pose[2] < tableZ_->value()) {
        *why = QStringLiteral("末端低于工作台面（Z=%1 mm）").arg(pose[2], 0, 'f', 1);
        return false;
    }
    return true;
}

QVector<TrajectoryPanel::PathPoint> TrajectoryPanel::interpolate(
    const QVector<PathPoint>& keys) const {
    QVector<PathPoint> out;
    if (keys.size() < 2) return out;

    const int interp = interpCombo_->currentIndex();
    for (int seg = 0; seg + 1 < keys.size(); ++seg) {
        const PathPoint& p0 = keys.at(seg);
        const PathPoint& p1 = keys.at(seg + 1);

        // 每段按自身弦长估计采样数，避免短段过稀、长段过密
        double segLen = 0.0;
        for (int k = 0; k < 3; ++k) {
            const double d = p1.pose[k] - p0.pose[k];
            segLen += d * d;
        }
        const int n = qMax(2, int(std::sqrt(segLen) / sampleStep_->value()));

        if (interp == 2 && seg + 3 < keys.size()) {
            // 三次贝塞尔：以连续 4 个关键点为控制点（末尾不足 4 点的段回退直线）
            const PathPoint& p2 = keys.at(seg + 2);
            const PathPoint& p3 = keys.at(seg + 3);
            for (int i = 0; i <= n; ++i) {
                const double t = double(i) / n;
                PathPoint pt;
                for (int k = 0; k < 6; ++k) {
                    pt.pose[k] = bezier(p0.pose[k], p1.pose[k], p2.pose[k], p3.pose[k], t);
                }
                pt.motion = p0.motion;
                pt.interp = 2;
                out.push_back(pt);
            }
        } else if (interp == 1 && seg + 2 < keys.size()) {
            // 圆弧：三点定弧，以二次贝塞尔逼近相邻两点间的弧段，
            // 大跨度圆弧需加密关键点分段（控制多边形足够细时逼近误差可忽略）
            const PathPoint& p2 = keys.at(seg + 2);
            const double A[3] = {p0.pose[0], p0.pose[1], p0.pose[2]};
            const double B[3] = {p1.pose[0], p1.pose[1], p1.pose[2]};
            const double C[3] = {p2.pose[0], p2.pose[1], p2.pose[2]};
            for (int i = 0; i <= n; ++i) {
                const double t = double(i) / n;
                const double u = 1.0 - t;
                PathPoint pt;
                for (int k = 0; k < 3; ++k) {
                    pt.pose[k] = u * u * A[k] + 2.0 * u * t * B[k] + t * t * C[k];
                }
                pt.pose[3] = p0.pose[3];
                pt.pose[4] = p0.pose[4];
                pt.pose[5] = p0.pose[5];
                pt.motion = p0.motion;
                pt.interp = 1;
                out.push_back(pt);
            }
        } else {
            for (int i = 0; i <= n; ++i) {
                const double t = double(i) / n;
                PathPoint pt;
                for (int k = 0; k < 6; ++k) {
                    pt.pose[k] = p0.pose[k] + t * (p1.pose[k] - p0.pose[k]);
                }
                pt.motion = p0.motion;
                pt.interp = 0;
                out.push_back(pt);
            }
        }
    }
    return out;
}

void TrajectoryPanel::onAddPoint() {
    const int row = pointTable_->rowCount();
    pointTable_->insertRow(row);
    const double demo[6] = {300.0 + 20.0 * row, 0.0, 250.0, 0.0, 0.0, 0.0};
    for (int c = 0; c < 6; ++c) {
        pointTable_->setItem(row, c, new QTableWidgetItem(QString::number(demo[c], 'f', 2)));
    }
}

void TrajectoryPanel::onRemovePoint() {
    const int row = pointTable_->currentRow();
    if (row >= 0) pointTable_->removeRow(row);
}

void TrajectoryPanel::onImportStep() {
#ifdef LX_HAVE_OCC
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("导入工件模型"), QString(), QStringLiteral("STEP 模型 (*.step *.stp)"));
    if (path.isEmpty()) return;
    STEPControl_Reader reader;
    if (reader.ReadFile(const_cast<char*>(path.toUtf8().constData())) != IFSelect_RetDone) {
        QMessageBox::warning(this, QStringLiteral("导入失败"), path);
        return;
    }
    reader.TransferRoots();
    const TopoDS_Shape& shape = reader.OneShape();
    int count = 0;
    for (TopExp_Explorer exp(shape, TopAbs_FACE); exp.More(); exp.Next()) ++count;
    logLine(QStringLiteral("已导入 STEP：%1，面片数 %2（可在三维视图点选表面生成路径）").arg(path).arg(count));
#else
    QMessageBox::information(this, QStringLiteral("STEP 导入"),
        QStringLiteral("STEP 工件导入依赖 OpenCASCADE。请在 monitor-engineer.pro 中启用：\n"
                       "DEFINES += LX_HAVE_OCC\n"
                       "INCLUDEPATH += <occ>/inc\n"
                       "LIBS += -L<occ>/win64/vc14/lib -lTKernel -lTKTopAlgo -lTKSTEP\n\n"
                       "未启用时可在轨迹规划工具中导入点云 CSV 生成轨迹点。"));
    logLine(QStringLiteral("STEP 导入未启用（缺少 OpenCASCADE 依赖）"));
#endif
}

void TrajectoryPanel::onLoadDh() {
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("载入 DH 参数"), QString(), QStringLiteral("参数文件 (*.json)"));
    if (path.isEmpty()) return;

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, QStringLiteral("载入失败"), path);
        return;
    }
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    // 期望格式：{"dh": [[a,alpha,d,offset] x 6], "limits": [[min,max] x 6](可选)}
    const QJsonArray dh = o.value(QStringLiteral("dh")).toArray();
    if (dh.size() != 6) {
        QMessageBox::warning(this, QStringLiteral("格式错误"),
                             QStringLiteral("dh 数组需要 6 行 [a(mm), alpha(rad), d(mm), offset(rad)]"));
        return;
    }
    QVector<DhRow> parsed;
    for (int i = 0; i < 6; ++i) {
        const QJsonArray row = dh.at(i).toArray();
        if (row.size() != 4) {
            QMessageBox::warning(this, QStringLiteral("格式错误"),
                                 QStringLiteral("第 %1 行参数需要 4 列").arg(i + 1));
            return;
        }
        parsed.push_back(DhRow{row.at(0).toDouble(), row.at(1).toDouble(),
                               row.at(2).toDouble(), row.at(3).toDouble()});
    }
    dh_ = parsed;

    const QJsonArray limits = o.value(QStringLiteral("limits")).toArray();
    if (limits.size() == 6) {
        for (int i = 0; i < 6; ++i) {
            const QJsonArray lim = limits.at(i).toArray();
            if (lim.size() == 2) {
                jointLimit_[i][0] = lim.at(0).toDouble();
                jointLimit_[i][1] = lim.at(1).toDouble();
            }
        }
    }
    dhLoaded_ = true;
    logLine(QStringLiteral("已载入 DH 参数：%1（6 行，限位%2）")
                .arg(path).arg(limits.size() == 6 ? QStringLiteral("已更新")
                                                  : QStringLiteral("保持默认")));
}

void TrajectoryPanel::onSimulate() {
    if (!dhLoaded_) {
        QMessageBox::warning(this, QStringLiteral("缺少参数"),
                             QStringLiteral("请先载入机械臂 DH 参数"));
        return;
    }
    keys_.clear();
    for (int r = 0; r < pointTable_->rowCount(); ++r) {
        PathPoint p;
        for (int c = 0; c < 6; ++c) {
            p.pose[c] = pointTable_->item(r, c) ? pointTable_->item(r, c)->text().toDouble() : 0.0;
        }
        p.motion = modeCombo_->currentIndex();
        p.interp = interpCombo_->currentIndex();
        keys_.push_back(p);
    }
    if (keys_.size() < 2) {
        QMessageBox::warning(this, QStringLiteral("数据不足"), QStringLiteral("至少需要 2 个关键点"));
        return;
    }

    path_ = interpolate(keys_);

    // 逐点 IK 传播：首点多起点求解，之后用上一点解做种子，
    // 保证整条轨迹的解分支连续（分支跳变在实机上等于关节飞转）
    int bad = 0;
    QString firstWhy;
    double prevQ[6] = {};
    bool havePrev = false;
    for (const PathPoint& p : path_) {
        double q[6] = {};
        const bool ok = inverseKinematics(p.pose, q, havePrev ? prevQ : nullptr);
        if (ok) {
            for (int i = 0; i < 6; ++i) prevQ[i] = q[i];
            havePrev = true;
        } else {
            ++bad;
            havePrev = false;   // 断链后下一点重新多起点求初解
            if (firstWhy.isEmpty()) firstWhy = QStringLiteral("逆解不收敛");
            continue;
        }
        QString why;
        if (!checkPoint(q, &why)) {
            ++bad;
            if (firstWhy.isEmpty()) firstWhy = why;
        }
    }

    const int total = path_.size();
    if (bad == 0) {
        pathReady_ = true;
        simSummary_->setText(QStringLiteral("仿真通过：轨迹点 %1 个，无超限位/奇异/工作台碰撞。")
                                 .arg(total));
        simSummary_->setStyleSheet(QStringLiteral("color: #2E7D32;"));
        logLine(QStringLiteral("仿真通过，共 %1 个轨迹点").arg(total));
    } else {
        pathReady_ = false;
        simSummary_->setText(QStringLiteral("仿真未通过：%1/%2 个点存在问题，首例：%3\n"
                                             "（仿真不通过，禁止下发）")
                                 .arg(bad).arg(total).arg(firstWhy));
        simSummary_->setStyleSheet(QStringLiteral("color: #C62828;"));
        logLine(QStringLiteral("仿真失败：%1").arg(firstWhy));
    }
}

void TrajectoryPanel::onGenerate() {
    if (path_.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("请先完成离线仿真"));
        return;
    }
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("导出轨迹文件"), QStringLiteral("trajectory.csv"),
        QStringLiteral("轨迹点 CSV (*.csv)"));
    if (path.isEmpty()) return;

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, QStringLiteral("导出失败"), path);
        return;
    }
    // 列序与 UdpControllerClient::loadTrajectoryFile / trajectory-planner 一致
    f.write("index,x,y,z,rx,ry,rz,motion\n");
    for (int i = 0; i < path_.size(); ++i) {
        const PathPoint& p = path_.at(i);
        f.write(QStringLiteral("%1,%2,%3,%4,%5,%6,%7,%8\n")
                    .arg(i)
                    .arg(p.pose[0], 0, 'f', 3).arg(p.pose[1], 0, 'f', 3).arg(p.pose[2], 0, 'f', 3)
                    .arg(p.pose[3], 0, 'f', 5).arg(p.pose[4], 0, 'f', 5).arg(p.pose[5], 0, 'f', 5)
                    .arg(p.motion)
                    .toUtf8());
    }
    logLine(QStringLiteral("已导出轨迹：%1（%2 点）").arg(path).arg(path_.size()));
}

void TrajectoryPanel::onDownload() {
    if (!pathReady_) {
        QMessageBox::warning(this, QStringLiteral("禁止下发"),
                             QStringLiteral("仿真未通过或尚未仿真，不能下发到实体机器人"));
        return;
    }
    if (!ctrl_->isOnline()) {
        QMessageBox::warning(this, QStringLiteral("控制器离线"),
                             QStringLiteral("控制器不在线，无法下发轨迹。"));
        return;
    }
    // 二进制载荷：int32 点数 + 逐点 double[6]（mm/rad），格式见 FirmwareProtocol.h
    QByteArray payload;
    const qint32 n = qint32(path_.size());
    payload.append(reinterpret_cast<const char*>(&n), sizeof(n));
    for (const PathPoint& p : path_) {
        payload.append(reinterpret_cast<const char*>(p.pose), sizeof(p.pose));
    }
    QString err;
    if (!ctrl_->sendCommand(lx::protocol::CmdId::LoadTrajectory, payload, &err)) {
        QMessageBox::warning(this, QStringLiteral("下发失败"), err);
        return;
    }
    logLine(QStringLiteral("已下发轨迹：%1 个点").arg(path_.size()));
}

void TrajectoryPanel::logLine(const QString& text) {
    const int row = logView_->rowCount();
    logView_->insertRow(row);
    logView_->setItem(row, 0, new QTableWidgetItem(text));
    logView_->scrollToBottom();
}
