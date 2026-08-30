// 实验数据分析：力跟踪误差 / 轨迹跟踪误差 / 双组实验对照
#include <QApplication>
#include <QChart>
#include <QChartView>
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
#include <QLineSeries>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QValueAxis>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
using namespace QtCharts;   // Qt 5：QtCharts 类位于 QtCharts 命名空间；Qt 6 无该命名空间
#endif

namespace {

struct Series {
    QVector<double> t;
    QVector<double> force;      // 选定通道的力
};

// 读取 CsvLogger 输出的运行日志
Series loadLog(const QString& path, const QString& channel, QString* err) {
    Series s;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        *err = QStringLiteral("无法打开：%1").arg(path);
        return s;
    }
    f.readLine();   // 跳过表头

    const int colFx = 22, colFy = 23, colFz = 24;
    while (!f.atEnd()) {
        const QList<QByteArray> c = f.readLine().trimmed().split(',');
        if (c.size() < 35) continue;
        double v = 0.0;
        if (channel == QStringLiteral("Fz")) {
            v = c.at(colFz).toDouble();
        } else if (channel == QStringLiteral("Fx")) {
            v = c.at(colFx).toDouble();
        } else if (channel == QStringLiteral("Fy")) {
            v = c.at(colFy).toDouble();
        } else {
            const double fx = c.at(colFx).toDouble();
            const double fy = c.at(colFy).toDouble();
            const double fz = c.at(colFz).toDouble();
            v = std::sqrt(fx * fx + fy * fy + fz * fz);
        }
        s.t.push_back(c.at(1).toDouble() / 1.0e6);
        s.force.push_back(v);
    }
    if (s.t.isEmpty()) {
        *err = QStringLiteral("日志为空或格式不匹配：%1").arg(path);
        return s;
    }
    const double t0 = s.t.first();
    for (double& x : s.t) x -= t0;
    return s;
}

struct Metrics {
    double rms = 0.0;
    double mae = 0.0;
    double maxAbs = 0.0;
    double steadyRms = 0.0;   // 后 50% 数据的 RMS
    double overshoot = 0.0;   // 相对目标的最大超调百分比
};

Metrics evaluate(const Series& s, double target) {
    Metrics m;
    if (s.force.isEmpty()) return m;

    const int n = s.force.size();
    const int nSteady = qMax(1, n / 2);
    double sumSq = 0.0, sumAbs = 0.0, sumSqSteady = 0.0;
    for (int i = 0; i < n; ++i) {
        const double e = s.force.at(i) - target;
        sumSq += e * e;
        sumAbs += std::fabs(e);
        if (i >= n - nSteady) sumSqSteady += e * e;
        m.maxAbs = std::max(m.maxAbs, std::fabs(e));
        m.overshoot = std::max(m.overshoot, 100.0 * e / std::max(1e-6, target));
    }
    m.rms = std::sqrt(sumSq / n);
    m.mae = sumAbs / n;
    m.steadyRms = std::sqrt(sumSqSteady / nSteady);
    return m;
}

}  // namespace

class AnalyzerWindow : public QWidget {
public:
    explicit AnalyzerWindow(QWidget* parent = nullptr) : QWidget(parent) {
        auto* root = new QVBoxLayout(this);

        // —— 输入区 ——
        auto* io = new QGroupBox(QStringLiteral("实验数据（A：无鲁棒控制器；B：有鲁棒控制器）"), this);
        auto* f = new QFormLayout(io);
        auto mkRow = [this](QLineEdit*& edit) {
            auto* box = new QHBoxLayout;
            edit = new QLineEdit(this);
            auto* btn = new QPushButton(QStringLiteral("选择日志"), this);
            box->addWidget(edit, 1);
            box->addWidget(btn);
            // 注意捕获 edit 的当前值（QLineEdit*）而非引用：lambda 会在
            // mkRow 返回后才被触发，按引用捕获局部参数会悬垂
            connect(btn, &QPushButton::clicked, this, [this, edit] {
                const QString p = QFileDialog::getOpenFileName(
                    this, QStringLiteral("选择运行日志"), QString(), QStringLiteral("CSV 日志 (*.csv)"));
                if (!p.isEmpty()) edit->setText(p);
            });
            return box;
        };
        f->addRow(QStringLiteral("A 组日志"), mkRow(editA_));
        f->addRow(QStringLiteral("B 组日志"), mkRow(editB_));

        target_ = new QDoubleSpinBox(this);
        target_->setRange(-200.0, 200.0);
        target_->setValue(20.0);
        target_->setSuffix(QStringLiteral(" N"));
        channel_ = new QComboBox(this);
        channel_->addItems({QStringLiteral("合力 |F|"), QStringLiteral("Fx"),
                            QStringLiteral("Fy"), QStringLiteral("Fz")});
        f->addRow(QStringLiteral("目标力"), target_);
        f->addRow(QStringLiteral("分析通道"), channel_);
        root->addWidget(io);

        auto* run = new QHBoxLayout;
        auto* calcBtn = new QPushButton(QStringLiteral("分析并绘图"), this);
        auto* expBtn = new QPushButton(QStringLiteral("导出报告 CSV"), this);
        info_ = new QLabel(QStringLiteral("等待数据"), this);
        run->addWidget(calcBtn);
        run->addWidget(expBtn);
        run->addStretch();
        run->addWidget(info_);
        root->addLayout(run);

        // —— 图表 ——
        auto* charts = new QHBoxLayout;
        charts->addWidget(buildChart(QStringLiteral("力跟踪曲线"), &viewForce_));
        charts->addWidget(buildChart(QStringLiteral("跟踪误差曲线"), &viewError_));
        root->addLayout(charts, 3);

        // —— 指标表 ——
        table_ = new QTableWidget(0, 6, this);
        table_->setHorizontalHeaderLabels(
            {QStringLiteral("组别"), QStringLiteral("RMS 误差 (N)"), QStringLiteral("平均绝对误差 (N)"),
             QStringLiteral("最大偏差 (N)"), QStringLiteral("稳态 RMS (N)"), QStringLiteral("最大超调 (%)")});
        table_->horizontalHeader()->setStretchLastSection(true);
        table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        root->addWidget(table_, 1);

        connect(calcBtn, &QPushButton::clicked, this, &AnalyzerWindow::onAnalyze);
        connect(expBtn, &QPushButton::clicked, this, &AnalyzerWindow::onExport);

        setWindowTitle(QStringLiteral("灵犀智磨 实验数据分析工具 v%1").arg(QStringLiteral(LX_VERSION_STR)));
        resize(1200, 760);
    }

private:
    QWidget* buildChart(const QString& title, QChartView** out) {
        auto* chart = new QChart;
        chart->setTitle(title);
        chart->createDefaultAxes();
        auto* view = new QChartView(chart, this);
        view->setRenderHint(QPainter::Antialiasing);
        *out = view;
        return view;
    }

    void onAnalyze() {
        QString err;
        const QString ch = channel_->currentText();
        seriesA_ = loadLog(editA_->text(), ch, &err);
        if (seriesA_.force.isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("读取失败"), err);
            return;
        }
        seriesB_ = editB_->text().isEmpty() ? Series{} : loadLog(editB_->text(), ch, &err);
        metricsA_ = evaluate(seriesA_, target_->value());
        metricsB_ = seriesB_.force.isEmpty() ? Metrics{} : evaluate(seriesB_, target_->value());

        drawCurves();
        fillTable();
        info_->setText(QStringLiteral("A 组 %1 点，B 组 %2 点")
                           .arg(seriesA_.force.size()).arg(seriesB_.force.size()));
    }

    void drawCurves() {
        auto* cf = viewForce_->chart();
        cf->removeAllSeries();
        auto* ce = viewError_->chart();
        ce->removeAllSeries();

        auto add = [](QChart* c, const Series& s, const QString& name, bool error, double target) {
            auto* line = new QLineSeries;
            line->setName(name);
            for (int i = 0; i < s.t.size(); ++i) {
                const double y = error ? s.force.at(i) - target : s.force.at(i);
                line->append(s.t.at(i), y);
            }
            c->addSeries(line);
            return line;
        };

        add(cf, seriesA_, QStringLiteral("A 组（无鲁棒）"), false, target_->value());
        add(ce, seriesA_, QStringLiteral("A 组误差"), true, target_->value());
        if (!seriesB_.force.isEmpty()) {
            add(cf, seriesB_, QStringLiteral("B 组（有鲁棒）"), false, target_->value());
            add(ce, seriesB_, QStringLiteral("B 组误差"), true, target_->value());
        }

        // 目标参考线
        auto* ref = new QLineSeries;
        ref->setName(QStringLiteral("目标力"));
        if (!seriesA_.t.isEmpty()) {
            ref->append(seriesA_.t.first(), target_->value());
            ref->append(seriesA_.t.last(), target_->value());
        }
        cf->addSeries(ref);

        for (QChart* c : {cf, ce}) {
            // removeAllSeries 后旧坐标轴可能残留且与系列脱钩，
            // 全部移除后按新系列重建，避免二次分析时曲线不显示
            const auto axesList = c->axes();
            for (QAbstractAxis* a : axesList) c->removeAxis(a);
            c->createDefaultAxes();
            c->axes(Qt::Horizontal).first()->setTitleText(QStringLiteral("t / s"));
            c->axes(Qt::Vertical).first()->setTitleText(QStringLiteral("N"));
        }
    }

    void fillTable() {
        table_->setRowCount(0);
        auto row = [this](const QString& name, const Metrics& m) {
            const int r = table_->rowCount();
            table_->insertRow(r);
            table_->setItem(r, 0, new QTableWidgetItem(name));
            table_->setItem(r, 1, new QTableWidgetItem(QString::number(m.rms, 'f', 3)));
            table_->setItem(r, 2, new QTableWidgetItem(QString::number(m.mae, 'f', 3)));
            table_->setItem(r, 3, new QTableWidgetItem(QString::number(m.maxAbs, 'f', 3)));
            table_->setItem(r, 4, new QTableWidgetItem(QString::number(m.steadyRms, 'f', 3)));
            table_->setItem(r, 5, new QTableWidgetItem(QString::number(m.overshoot, 'f', 2)));
        };
        row(QStringLiteral("A 组（无鲁棒）"), metricsA_);
        if (!seriesB_.force.isEmpty()) {
            row(QStringLiteral("B 组（有鲁棒）"), metricsB_);
            const double gain = metricsA_.rms > 1e-9
                ? 100.0 * (metricsA_.rms - metricsB_.rms) / metricsA_.rms : 0.0;
            const int r = table_->rowCount();
            table_->insertRow(r);
            table_->setItem(r, 0, new QTableWidgetItem(QStringLiteral("RMS 改善")));
            table_->setItem(r, 1, new QTableWidgetItem(QStringLiteral("%1%").arg(gain, 0, 'f', 2)));
        }
    }

    void onExport() {
        if (seriesA_.force.isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("请先完成分析"));
            return;
        }
        const QString path = QFileDialog::getSaveFileName(
            this, QStringLiteral("导出分析报告"), QStringLiteral("analysis_report.csv"),
            QStringLiteral("CSV 报告 (*.csv)"));
        if (path.isEmpty()) return;

        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QMessageBox::warning(this, QStringLiteral("导出失败"), path);
            return;
        }
        f.write("group,t,force,error\n");
        auto dump = [&](const QString& g, const Series& s) {
            for (int i = 0; i < s.t.size(); ++i) {
                f.write(QStringLiteral("%1,%2,%3,%4\n")
                            .arg(g).arg(s.t.at(i), 0, 'f', 6)
                            .arg(s.force.at(i), 0, 'f', 4)
                            .arg(s.force.at(i) - target_->value(), 0, 'f', 4).toUtf8());
            }
        };
        dump(QStringLiteral("A"), seriesA_);
        if (!seriesB_.force.isEmpty()) dump(QStringLiteral("B"), seriesB_);
        info_->setText(QStringLiteral("已导出报告：%1").arg(path));
    }

    QLineEdit* editA_ = nullptr;
    QLineEdit* editB_ = nullptr;
    QDoubleSpinBox* target_ = nullptr;
    QComboBox* channel_ = nullptr;
    QChartView* viewForce_ = nullptr;
    QChartView* viewError_ = nullptr;
    QTableWidget* table_ = nullptr;
    QLabel* info_ = nullptr;

    Series seriesA_, seriesB_;
    Metrics metricsA_, metricsB_;
};

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("DataAnalyzer"));
    QApplication::setApplicationVersion(QStringLiteral(LX_VERSION_STR));
    QApplication::setOrganizationName(QStringLiteral(LX_ORG_NAME));

    AnalyzerWindow w;
    w.show();
    return app.exec();
}
