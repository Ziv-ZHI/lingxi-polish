// 工艺数据库管理工具：SQLite 工艺库检索、维护与导出
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QSqlTableModel>
#include <QStandardPaths>
#include <QTableView>
#include <QVBoxLayout>

#include "model/ProcessRecipe.h"

namespace {

bool ensureSchema(QSqlDatabase& db, QString* err) {
    QSqlQuery q(db);
    const QString sql = QStringLiteral(
        "CREATE TABLE IF NOT EXISTS process ("
        "  id TEXT PRIMARY KEY,"
        "  material TEXT NOT NULL,"
        "  surface TEXT,"
        "  tool TEXT,"
        "  forceN REAL,"
        "  feed REAL,"
        "  rpm REAL,"
        "  passes INTEGER,"
        "  roughness REAL,"
        "  note TEXT"
        ")");
    if (!q.exec(sql)) {
        *err = q.lastError().text();
        return false;
    }
    return true;
}

}  // namespace

class ProcessDbWindow : public QWidget {
public:
    explicit ProcessDbWindow(QWidget* parent = nullptr) : QWidget(parent) {
        const QString dir =
            QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
        QDir().mkpath(dir);
        const QString dbPath = dir + QStringLiteral("/process.db");

        db_ = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"));
        db_.setDatabaseName(dbPath);
        if (!db_.open()) {
            QMessageBox::critical(this, QStringLiteral("数据库错误"), db_.lastError().text());
            return;
        }
        QString err;
        if (!ensureSchema(db_, &err)) {
            QMessageBox::critical(this, QStringLiteral("建表失败"), err);
            return;
        }
        QSqlQuery count(db_);
        if (count.exec(QStringLiteral("SELECT COUNT(*) FROM process")) && count.next() &&
            count.value(0).toInt() == 0) {
            seed();
        }

        model_ = new QSqlTableModel(this, db_);
        model_->setTable(QStringLiteral("process"));
        model_->setEditStrategy(QSqlTableModel::OnManualSubmit);
        model_->select();
        model_->setHeaderData(0, Qt::Horizontal, QStringLiteral("工艺编号"));
        model_->setHeaderData(1, Qt::Horizontal, QStringLiteral("材料"));
        model_->setHeaderData(2, Qt::Horizontal, QStringLiteral("曲面特征"));
        model_->setHeaderData(3, Qt::Horizontal, QStringLiteral("打磨头"));
        model_->setHeaderData(4, Qt::Horizontal, QStringLiteral("恒力 (N)"));
        model_->setHeaderData(5, Qt::Horizontal, QStringLiteral("进给 (mm/s)"));
        model_->setHeaderData(6, Qt::Horizontal, QStringLiteral("转速 (rpm)"));
        model_->setHeaderData(7, Qt::Horizontal, QStringLiteral("遍数"));
        model_->setHeaderData(8, Qt::Horizontal, QStringLiteral("粗糙度 Ra"));
        model_->setHeaderData(9, Qt::Horizontal, QStringLiteral("备注"));

        auto* root = new QVBoxLayout(this);

        auto* filter = new QHBoxLayout;
        filter->addWidget(new QLabel(QStringLiteral("材料"), this));
        materialCombo_ = new QComboBox(this);
        materialCombo_->addItems({QStringLiteral("全部"), QStringLiteral("铝合金"),
                                  QStringLiteral("钢材"), QStringLiteral("木材"),
                                  QStringLiteral("复合材料")});
        filter->addWidget(materialCombo_);
        filter->addWidget(new QLabel(QStringLiteral("关键字"), this));
        keyword_ = new QLineEdit(this);
        keyword_->setPlaceholderText(QStringLiteral("按曲面特征或备注检索"));
        filter->addWidget(keyword_, 1);
        auto* searchBtn = new QPushButton(QStringLiteral("检索"), this);
        filter->addWidget(searchBtn);
        root->addLayout(filter);

        view_ = new QTableView(this);
        view_->setModel(model_);
        view_->horizontalHeader()->setStretchLastSection(true);
        view_->setSelectionBehavior(QAbstractItemView::SelectRows);
        root->addWidget(view_, 1);

        auto* ops = new QHBoxLayout;
        auto* addBtn = new QPushButton(QStringLiteral("新增工艺"), this);
        auto* delBtn = new QPushButton(QStringLiteral("删除选中"), this);
        auto* saveBtn = new QPushButton(QStringLiteral("保存修改"), this);
        auto* expBtn = new QPushButton(QStringLiteral("导出为上位机工艺配置"), this);
        info_ = new QLabel(QStringLiteral("数据库：%1").arg(dbPath), this);
        ops->addWidget(addBtn);
        ops->addWidget(delBtn);
        ops->addWidget(saveBtn);
        ops->addWidget(expBtn);
        ops->addStretch();
        ops->addWidget(info_);
        root->addLayout(ops);

        connect(searchBtn, &QPushButton::clicked, this, &ProcessDbWindow::onSearch);
        connect(addBtn, &QPushButton::clicked, this, &ProcessDbWindow::onAdd);
        connect(delBtn, &QPushButton::clicked, this, &ProcessDbWindow::onDelete);
        connect(saveBtn, &QPushButton::clicked, this, &ProcessDbWindow::onSave);
        connect(expBtn, &QPushButton::clicked, this, &ProcessDbWindow::onExport);

        setWindowTitle(QStringLiteral("灵犀智磨 工艺数据库 v%1").arg(QStringLiteral(LX_VERSION_STR)));
        resize(1100, 640);
    }

private:
    void seed() {
        // 首次运行写入几条示例工艺：铝合金 / 钢材 / 木材
        const char* rows[] = {
            "INSERT INTO process VALUES('AL-PLATE-001','铝合金','平板','千叶轮',18,15,3000,2,0.8,'平面粗抛')",
            "INSERT INTO process VALUES('AL-CURVE-001','铝合金','自由曲面','砂带',12,10,2500,3,0.6,'曲面精抛，姿态自适应')",
            "INSERT INTO process VALUES('ST-PLATE-001','钢材','平板','砂盘',32,12,2800,3,1.2,'不锈钢拉丝')",
            "INSERT INTO process VALUES('WD-CURVE-001','木材','异形曲面','砂带',8,20,1800,2,2.0,'木材去毛刺')"};
        for (const char* sql : rows) QSqlQuery(db_).exec(QString::fromUtf8(sql));
    }

    void onSearch() {
        QString where;
        if (materialCombo_->currentIndex() > 0) {
            where += QStringLiteral("material = '%1'").arg(materialCombo_->currentText());
        }
        const QString kw = keyword_->text().trimmed();
        if (!kw.isEmpty()) {
            if (!where.isEmpty()) where += QStringLiteral(" AND ");
            where += QStringLiteral("(surface LIKE '%%1%' OR note LIKE '%%1%')").arg(kw);
        }
        model_->setFilter(where);
        model_->select();
        info_->setText(QStringLiteral("检索结果 %1 条").arg(model_->rowCount()));
    }

    void onAdd() {
        const int row = model_->rowCount();
        model_->insertRow(row);
        model_->setData(model_->index(row, 0), QStringLiteral("NEW-%1").arg(row + 1));
        model_->setData(model_->index(row, 1), materialCombo_->currentIndex() > 0
                                                    ? materialCombo_->currentText()
                                                    : QStringLiteral("铝合金"));
        model_->setData(model_->index(row, 4), 20.0);
        model_->setData(model_->index(row, 5), 15.0);
        model_->setData(model_->index(row, 6), 3000.0);
        model_->setData(model_->index(row, 7), 2);
        view_->selectRow(row);
    }

    void onDelete() {
        const int row = view_->currentIndex().row();
        if (row < 0) return;
        model_->removeRow(row);
    }

    void onSave() {
        if (!model_->submitAll()) {
            QMessageBox::warning(this, QStringLiteral("保存失败"), model_->lastError().text());
            return;
        }
        model_->select();
        info_->setText(QStringLiteral("已保存，当前 %1 条工艺").arg(model_->rowCount()));
    }

    void onExport() {
        const int row = view_->currentIndex().row();
        if (row < 0) {
            QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("请先选中一行"));
            return;
        }
        const QSqlRecord rec = model_->record(row);

        lx::ProcessRecipe r;
        r.id = rec.value(QStringLiteral("id")).toString();
        r.name = QStringLiteral("%1-%2").arg(rec.value(QStringLiteral("material")).toString(),
                                             rec.value(QStringLiteral("surface")).toString());
        r.workpiece = rec.value(QStringLiteral("surface")).toString();
        r.targetForceN = rec.value(QStringLiteral("forceN")).toDouble();
        r.feedSpeedMmS = rec.value(QStringLiteral("feed")).toDouble();
        r.spindleRpm = rec.value(QStringLiteral("rpm")).toDouble();
        r.passes = rec.value(QStringLiteral("passes")).toInt();
        r.trajectoryFile = QStringLiteral("%1.csv").arg(r.id);

        const QString path = QFileDialog::getSaveFileName(
            this, QStringLiteral("导出工艺配置"), QStringLiteral("%1.json").arg(r.id),
            QStringLiteral("工艺配置 (*.json)"));
        if (path.isEmpty()) return;

        QString err;
        if (!lx::saveRecipeJson(r, path, &err)) {
            QMessageBox::warning(this, QStringLiteral("导出失败"), err);
            return;
        }
        info_->setText(QStringLiteral("已导出：%1（可直接拷入上位机工艺库目录）").arg(path));
    }

    QSqlDatabase db_;
    QSqlTableModel* model_ = nullptr;
    QTableView* view_ = nullptr;
    QComboBox* materialCombo_ = nullptr;
    QLineEdit* keyword_ = nullptr;
    QLabel* info_ = nullptr;
};

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("ProcessDB"));
    QApplication::setApplicationVersion(QStringLiteral(LX_VERSION_STR));
    QApplication::setOrganizationName(QStringLiteral(LX_ORG_NAME));

    ProcessDbWindow w;
    w.show();
    return app.exec();
}
