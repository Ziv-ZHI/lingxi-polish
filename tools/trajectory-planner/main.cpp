// 轨迹规划工具：点云 -> 分层扫描线（锯齿路径）-> 轨迹点文件
#include <QApplication>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace {

struct Vec3 {
    double x = 0.0, y = 0.0, z = 0.0;
};

struct PathRow {
    double p[6] = {};   // x y z rx ry rz
    int rowIndex = 0;
};

Vec3 normalize(const Vec3& v) {
    const double n = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    if (n < 1e-9) return {0.0, 0.0, 1.0};
    return {v.x / n, v.y / n, v.z / n};
}

Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

}  // namespace

class PlannerWindow : public QWidget {
public:
    explicit PlannerWindow(QWidget* parent = nullptr) : QWidget(parent) {
        auto* root = new QVBoxLayout(this);

        auto* io = new QGroupBox(QStringLiteral("工件输入"), this);
        auto* ioLay = new QHBoxLayout(io);
        fileEdit_ = new QLineEdit(this);
        fileEdit_->setPlaceholderText(QStringLiteral("点云 CSV：每行 x,y,z"));
        auto* openBtn = new QPushButton(QStringLiteral("选择点云文件"), this);
        ioLay->addWidget(fileEdit_, 1);
        ioLay->addWidget(openBtn);
        root->addWidget(io);

        auto* param = new QGroupBox(QStringLiteral("轨迹参数"), this);
        auto* f = new QFormLayout(param);
        toolRadius_ = new QDoubleSpinBox(this);
        toolRadius_->setRange(1.0, 200.0);
        toolRadius_->setValue(25.0);
        toolRadius_->setSuffix(QStringLiteral(" mm"));
        overlap_ = new QDoubleSpinBox(this);
        overlap_->setRange(0.0, 0.9);
        overlap_->setSingleStep(0.05);
        overlap_->setValue(0.3);
        margin_ = new QDoubleSpinBox(this);
        margin_->setRange(0.0, 200.0);
        margin_->setValue(5.0);
        margin_->setSuffix(QStringLiteral(" mm"));
        liftZ_ = new QDoubleSpinBox(this);
        liftZ_->setRange(0.0, 100.0);
        liftZ_->setValue(2.0);
        liftZ_->setSuffix(QStringLiteral(" mm"));
        dirCombo_ = new QComboBox(this);
        dirCombo_->addItem(QStringLiteral("沿 X 往复（行距沿 Y）"));
        dirCombo_->addItem(QStringLiteral("沿 Y 往复（行距沿 X）"));
        f->addRow(QStringLiteral("打磨头半径"), toolRadius_);
        f->addRow(QStringLiteral("行间重叠率"), overlap_);
        f->addRow(QStringLiteral("边界内缩"), margin_);
        f->addRow(QStringLiteral("抬刀高度"), liftZ_);
        f->addRow(QStringLiteral("走刀方向"), dirCombo_);
        root->addWidget(param);

        auto* run = new QHBoxLayout;
        auto* genBtn = new QPushButton(QStringLiteral("生成轨迹"), this);
        auto* expBtn = new QPushButton(QStringLiteral("导出轨迹点 CSV"), this);
        info_ = new QLabel(QStringLiteral("等待输入"), this);
        run->addWidget(genBtn);
        run->addWidget(expBtn);
        run->addStretch();
        run->addWidget(info_);
        root->addLayout(run);

        table_ = new QTableWidget(0, 7, this);
        table_->setHorizontalHeaderLabels(
            {QStringLiteral("序号"), QStringLiteral("X"), QStringLiteral("Y"),
             QStringLiteral("Z"), QStringLiteral("Rx"), QStringLiteral("Ry"),
             QStringLiteral("Rz")});
        table_->horizontalHeader()->setStretchLastSection(true);
        table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        root->addWidget(table_, 1);

        connect(openBtn, &QPushButton::clicked, this, &PlannerWindow::onOpenCloud);
        connect(genBtn, &QPushButton::clicked, this, &PlannerWindow::onGenerate);
        connect(expBtn, &QPushButton::clicked, this, &PlannerWindow::onExport);

        setWindowTitle(QStringLiteral("灵犀智磨 轨迹规划工具 v%1").arg(QStringLiteral(LX_VERSION_STR)));
        resize(1000, 680);
    }

private:
    void onOpenCloud() {
        const QString path = QFileDialog::getOpenFileName(
            this, QStringLiteral("选择点云文件"), QString(), QStringLiteral("点云 CSV (*.csv *.txt)"));
        if (path.isEmpty()) return;
        fileEdit_->setText(path);

        cloud_.clear();
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QMessageBox::warning(this, QStringLiteral("打开失败"), path);
            return;
        }
        while (!f.atEnd()) {
            const QByteArrayList parts = f.readLine().trimmed().split(',');
            if (parts.size() < 3) continue;
            bool ok1 = false, ok2 = false, ok3 = false;
            Vec3 p;
            p.x = parts[0].toDouble(&ok1);
            p.y = parts[1].toDouble(&ok2);
            p.z = parts[2].toDouble(&ok3);
            if (ok1 && ok2 && ok3) cloud_.push_back(p);
        }
        info_->setText(QStringLiteral("已载入 %1 个点云数据").arg(cloud_.size()));
    }

    void onGenerate() {
        if (cloud_.size() < 4) {
            QMessageBox::warning(this, QStringLiteral("数据不足"), QStringLiteral("点云至少需要 4 个点"));
            return;
        }
        path_.clear();

        const bool alongX = dirCombo_->currentIndex() == 0;
        const double pitch = 2.0 * toolRadius_->value() * (1.0 - overlap_->value());
        if (pitch < 0.1) {
            QMessageBox::warning(this, QStringLiteral("参数错误"), QStringLiteral("行距过小"));
            return;
        }

        // 1) 按行距分层（沿 X 走刀则按 Y 分层，反之亦然）
        double minV = 1e18, maxV = -1e18;
        for (const Vec3& p : cloud_) {
            const double v = alongX ? p.y : p.x;
            minV = std::min(minV, v);
            maxV = std::max(maxV, v);
        }
        const int rows = qMax(1, int((maxV - minV) / pitch) + 1);

        QVector<QVector<Vec3>> grid(rows);
        for (const Vec3& p : cloud_) {
            int r = int(((alongX ? p.y : p.x) - minV) / pitch);
            r = std::min(std::max(r, 0), rows - 1);
            grid[r].push_back(p);
        }

        // 2) 每层沿走刀方向排序，形成网格
        for (QVector<Vec3>& row : grid) {
            std::sort(row.begin(), row.end(), [alongX](const Vec3& a, const Vec3& b) {
                return alongX ? a.x < b.x : a.y < b.y;
            });
        }

        // 3) 锯齿连接：偶数行正序、奇数行逆序；法向由邻域叉乘估计
        for (int r = 0; r < rows; ++r) {
            QVector<Vec3> row = grid.at(r);
            if (row.isEmpty()) continue;
            if (r % 2 == 1) std::reverse(row.begin(), row.end());

            for (int c = 0; c < row.size(); ++c) {
                const Vec3& p = row.at(c);
                Vec3 tangent{1.0, 0.0, 0.0};
                Vec3 binormal{0.0, 1.0, 0.0};
                if (c + 1 < row.size()) {
                    tangent = {row.at(c + 1).x - p.x, row.at(c + 1).y - p.y, row.at(c + 1).z - p.z};
                }
                if (r + 1 < rows && !grid.at(r + 1).isEmpty()) {
                    const Vec3& q = grid.at(r + 1).at(qMin(c, grid.at(r + 1).size() - 1));
                    binormal = {q.x - p.x, q.y - p.y, q.z - p.z};
                }
                Vec3 n = normalize(cross(tangent, binormal));
                if (n.z < 0.0) n = {-n.x, -n.y, -n.z};   // 统一朝上

                PathRow pr;
                pr.p[0] = p.x + n.x * margin_->value();
                pr.p[1] = p.y + n.y * margin_->value();
                pr.p[2] = p.z + n.z * (margin_->value() + liftZ_->value());
                // 姿态：工具 z 轴对齐曲面法向（小角度近似，实机按机器人约定重算）
                pr.p[3] = std::atan2(n.y, n.z);
                pr.p[4] = std::atan2(-n.x, std::sqrt(n.y * n.y + n.z * n.z));
                pr.p[5] = 0.0;
                pr.rowIndex = r;
                path_.push_back(pr);
            }
        }

        table_->setRowCount(0);
        for (int i = 0; i < path_.size(); ++i) {
            const PathRow& pr = path_.at(i);
            table_->insertRow(i);
            table_->setItem(i, 0, new QTableWidgetItem(QString::number(i)));
            for (int k = 0; k < 6; ++k) {
                table_->setItem(i, k + 1,
                                new QTableWidgetItem(QString::number(pr.p[k], 'f', 3)));
            }
        }
        info_->setText(QStringLiteral("已生成 %1 个轨迹点（%2 行，行距 %3 mm）")
                           .arg(path_.size()).arg(rows).arg(pitch, 0, 'f', 2));
    }

    void onExport() {
        if (path_.isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("请先生成轨迹"));
            return;
        }
        const QString path = QFileDialog::getSaveFileName(
            this, QStringLiteral("导出轨迹点"), QStringLiteral("trajectory.csv"),
            QStringLiteral("轨迹点 CSV (*.csv)"));
        if (path.isEmpty()) return;

        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QMessageBox::warning(this, QStringLiteral("导出失败"), path);
            return;
        }
        f.write("index,x,y,z,rx,ry,rz,row\n");
        for (int i = 0; i < path_.size(); ++i) {
            const PathRow& pr = path_.at(i);
            f.write(QStringLiteral("%1,%2,%3,%4,%5,%6,%7,%8\n")
                        .arg(i)
                        .arg(pr.p[0], 0, 'f', 3).arg(pr.p[1], 0, 'f', 3).arg(pr.p[2], 0, 'f', 3)
                        .arg(pr.p[3], 0, 'f', 5).arg(pr.p[4], 0, 'f', 5).arg(pr.p[5], 0, 'f', 5)
                        .arg(pr.rowIndex).toUtf8());
        }
        info_->setText(QStringLiteral("已导出：%1").arg(path));
    }

    QLineEdit* fileEdit_ = nullptr;
    QDoubleSpinBox* toolRadius_ = nullptr;
    QDoubleSpinBox* overlap_ = nullptr;
    QDoubleSpinBox* margin_ = nullptr;
    QDoubleSpinBox* liftZ_ = nullptr;
    QComboBox* dirCombo_ = nullptr;
    QTableWidget* table_ = nullptr;
    QLabel* info_ = nullptr;

    QVector<Vec3> cloud_;
    QVector<PathRow> path_;
};

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("TrajectoryPlanner"));
    QApplication::setApplicationVersion(QStringLiteral(LX_VERSION_STR));
    QApplication::setOrganizationName(QStringLiteral(LX_ORG_NAME));

    PlannerWindow w;
    w.show();
    return app.exec();
}
