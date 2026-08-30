#include "OperatorWindow.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QStatusBar>
#include <QVBoxLayout>

#include <cmath>

namespace {

// 首次运行的演示轨迹：几段往复扫描线，保证"一键打磨"在没有 trajectory-planner
// 产出时也能全链路演示；实机工艺由工程师用轨迹模块/规划工具生成正式轨迹替换
void writeDemoTrajectoryIfMissing(const QString& path) {
    if (QFileInfo::exists(path)) return;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return;
    f.write("index,x,y,z,rx,ry,rz,row\n");
    int idx = 0;
    for (int row = 0; row < 3; ++row) {
        const double y = -40.0 + 40.0 * row;
        f.write(QStringLiteral("%1,%2,%3,%4,0,0,0,%5\n").arg(idx++)
                    .arg(250.0, 0, 'f', 1).arg(y, 0, 'f', 1).arg(200.0, 0, 'f', 1).arg(row).toUtf8());
        f.write(QStringLiteral("%1,%2,%3,%4,0,0,0,%5\n").arg(idx++)
                    .arg(410.0, 0, 'f', 1).arg(y, 0, 'f', 1).arg(200.0, 0, 'f', 1).arg(row).toUtf8());
    }
}

}  // namespace

OperatorWindow::OperatorWindow(lx::UserManager& users, QWidget* parent)
    : QMainWindow(parent), users_(users) {
    ctrl_.setController(QHostAddress(QStringLiteral("192.168.1.10")));
    if (QString err; !ctrl_.bindTelemetry(lx::protocol::kPortTele, &err)) {
        QMessageBox::warning(this, QStringLiteral("通信警告"), err);
    }

    recipesDir_ = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)
                  + QStringLiteral("/recipes");
    loadRecipes(recipesDir_);
    buildUi();

    QSettings stats;
    totalCount_ = stats.value(QStringLiteral("stats/total"), 0).toInt();
    goodCount_ = stats.value(QStringLiteral("stats/good"), 0).toInt();
    updateCounter();

    connect(&ctrl_, &lx::UdpControllerClient::telemetryReady, this, &OperatorWindow::onTelemetry);
    connect(&ctrl_, &lx::UdpControllerClient::alarmRaised, this, &OperatorWindow::onAlarm);

    setWindowTitle(QStringLiteral("灵犀智磨 打磨作业 v%1（操作工版）")
                       .arg(QStringLiteral(LX_VERSION_STR)));
    resize(1024, 760);
}

void OperatorWindow::loadRecipes(const QString& dir) {
    QDir().mkpath(dir);
    recipes_ = lx::loadRecipeLibrary(dir);

    if (recipes_.isEmpty()) {
        // 首次运行生成两份示例工艺 + 演示轨迹，便于现场立即试用
        lx::ProcessRecipe al;
        al.id = QStringLiteral("AL-PLATE-001");
        al.name = QStringLiteral("铝合金平板打磨");
        al.material = lx::MaterialType::Aluminum;
        al.targetForceN = 18.0;
        al.feedSpeedMmS = 15.0;
        al.trajectoryFile = QStringLiteral("traj_al_plate.csv");

        lx::ProcessRecipe st;
        st.id = QStringLiteral("ST-CURVE-001");
        st.name = QStringLiteral("钢制曲面打磨");
        st.material = lx::MaterialType::Steel;
        st.targetForceN = 32.0;
        st.feedSpeedMmS = 10.0;
        st.trajectoryFile = QStringLiteral("traj_st_curve.csv");

        QString err;
        lx::saveRecipeJson(al, dir + QStringLiteral("/AL-PLATE-001.json"), &err);
        lx::saveRecipeJson(st, dir + QStringLiteral("/ST-CURVE-001.json"), &err);
        writeDemoTrajectoryIfMissing(dir + QStringLiteral("/traj_al_plate.csv"));
        writeDemoTrajectoryIfMissing(dir + QStringLiteral("/traj_st_curve.csv"));
        recipes_ = lx::loadRecipeLibrary(dir);
    }
}

void OperatorWindow::buildUi() {
    auto* central = new QWidget(this);
    auto* root = new QVBoxLayout(central);

    // —— 第一步：选择工艺 ——
    auto* step1 = new QGroupBox(QStringLiteral("第 1 步：选择工件工艺"), this);
    auto* s1 = new QVBoxLayout(step1);
    recipeCombo_ = new QComboBox(this);
    for (const lx::ProcessRecipe& r : recipes_) {
        recipeCombo_->addItem(QStringLiteral("%1（%2）").arg(r.name, r.id),
                              QVariant::fromValue(r.id));
    }
    auto bigFont = recipeCombo_->font();
    bigFont.setPointSize(bigFont.pointSize() + 4);
    recipeCombo_->setFont(bigFont);
    recipeInfo_ = new QLabel(this);
    recipeInfo_->setWordWrap(true);
    s1->addWidget(recipeCombo_);
    s1->addWidget(recipeInfo_);
    root->addWidget(step1);

    // —— 第二步：上料确认 + 一键打磨 ——
    auto* step2 = new QGroupBox(QStringLiteral("第 2 步：上料确认后一键打磨"), this);
    auto* s2 = new QVBoxLayout(step2);
    loadCheck_ = new QCheckBox(QStringLiteral("已将工件装夹到位，并关闭防护门"), this);
    execBtn_ = new QPushButton(QStringLiteral("开始打磨"), this);
    stopBtn_ = new QPushButton(QStringLiteral("紧急停止"), this);
    stopBtn_->setObjectName(QStringLiteral("btnDanger"));   // 主题中的红色危险按钮
    execBtn_->setMinimumHeight(64);
    stopBtn_->setMinimumHeight(64);
    stopBtn_->setEnabled(false);
    progress_ = new QProgressBar(this);
    progress_->setRange(0, 100);
    progress_->setMinimumHeight(28);
    statusLabel_ = new QLabel(QStringLiteral("待机"), this);
    forceLabel_ = new QLabel(QStringLiteral("当前打磨力：-- N"), this);
    s2->addWidget(loadCheck_);
    s2->addWidget(execBtn_);
    s2->addWidget(stopBtn_);
    s2->addWidget(progress_);
    s2->addWidget(statusLabel_);
    s2->addWidget(forceLabel_);
    root->addWidget(step2);

    // —— 第三步：质量记录 ——
    auto* step3 = new QGroupBox(QStringLiteral("第 3 步：完成登记"), this);
    auto* s3 = new QHBoxLayout(step3);
    goodBtn_ = new QPushButton(QStringLiteral("良品"), this);
    rejectBtn_ = new QPushButton(QStringLiteral("不良品"), this);
    goodBtn_->setMinimumHeight(48);
    rejectBtn_->setMinimumHeight(48);
    counterLabel_ = new QLabel(this);
    s3->addWidget(goodBtn_);
    s3->addWidget(rejectBtn_);
    s3->addStretch();
    s3->addWidget(counterLabel_);
    root->addWidget(step3);

    root->addStretch();
    setCentralWidget(central);

    // 模拟遥测放在状态栏：现场作业不显眼，开发/演示随手可用
    simCheck_ = new QCheckBox(QStringLiteral("模拟遥测"), this);
    statusBar()->addPermanentWidget(simCheck_);
    connect(simCheck_, &QCheckBox::toggled, this, [this](bool on) {
        if (on) sim_.start(2);
        else    sim_.stop();
    });
    connect(&sim_, &lx::TelemetrySimulator::frameReady,
            &ctrl_, &lx::UdpControllerClient::injectFrame);

    connect(recipeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &OperatorWindow::onRecipeChanged);
    connect(loadCheck_, &QCheckBox::toggled, this, [this](bool) { updateExecEnabled(); });
    connect(execBtn_, &QPushButton::clicked, this, &OperatorWindow::onExecute);
    connect(stopBtn_, &QPushButton::clicked, this, &OperatorWindow::onStop);
    connect(goodBtn_, &QPushButton::clicked, this, &OperatorWindow::onMarkGood);
    connect(rejectBtn_, &QPushButton::clicked, this, &OperatorWindow::onMarkReject);

    onRecipeChanged(recipeCombo_->currentIndex());
}

void OperatorWindow::onRecipeChanged(int index) {
    if (index < 0 || index >= recipes_.size()) {
        recipeInfo_->setText(QStringLiteral("工艺库为空，请联系工程师导入工艺配置"));
        updateExecEnabled();
        return;
    }
    const lx::ProcessRecipe& r = recipes_.at(index);
    recipeInfo_->setText(QStringLiteral(
        "材料：%1　|　目标恒力：%2 N　|　进给速度：%3 mm/s　|　打磨头转速：%4 rpm")
        .arg(r.materialName()).arg(r.targetForceN, 0, 'f', 1)
        .arg(r.feedSpeedMmS, 0, 'f', 1).arg(r.spindleRpm, 0, 'f', 0));
    updateExecEnabled();
}

void OperatorWindow::updateExecEnabled() {
    execBtn_->setEnabled(!busy_ && loadCheck_ && loadCheck_->isChecked()
                         && recipeCombo_->currentIndex() >= 0);
}

void OperatorWindow::setBusy(bool busy) {
    busy_ = busy;
    stopBtn_->setEnabled(busy);
    recipeCombo_->setEnabled(!busy);
    loadCheck_->setEnabled(!busy);
    goodBtn_->setEnabled(!busy);
    rejectBtn_->setEnabled(!busy);
    updateExecEnabled();
}

void OperatorWindow::onExecute() {
    const int idx = recipeCombo_->currentIndex();
    if (idx < 0 || idx >= recipes_.size()) return;
    if (!loadCheck_->isChecked()) {
        QMessageBox::information(this, QStringLiteral("请先上料"),
                                 QStringLiteral("请确认工件已装夹并关闭防护门后再启动。"));
        return;
    }

    QString err;
    if (!ctrl_.uploadRecipe(recipes_.at(idx), recipesDir_, &err) ||
        !ctrl_.setTaskMode(lx::protocol::TaskMode::Polishing, &err) ||
        !ctrl_.sendCommand(lx::protocol::CmdId::Start, {}, &err)) {
        QMessageBox::critical(this, QStringLiteral("启动失败"), err);
        return;
    }
    setBusy(true);
    statusLabel_->setText(QStringLiteral("打磨中，请勿打开防护门"));
}

void OperatorWindow::onStop() {
    QString err;
    if (!ctrl_.sendCommand(lx::protocol::CmdId::Stop, {}, &err)) {
        QMessageBox::warning(this, QStringLiteral("停止失败"), err);
        return;
    }
    setBusy(false);
    statusLabel_->setText(QStringLiteral("已停止"));
}

void OperatorWindow::onTelemetry(const lx::protocol::TelemetryFrame& f) {
    // 力值显示做 10:1 降采样（2ms -> 20ms），进度条按控制器上报值更新
    static int skip = 0;
    if (++skip % 10 != 0) return;

    const double fn = std::sqrt(f.force[0] * f.force[0] + f.force[1] * f.force[1] +
                                f.force[2] * f.force[2]);
    forceLabel_->setText(QStringLiteral("当前打磨力：%1 N").arg(fn, 0, 'f', 2));
    progress_->setValue(f.progress);

    if (busy_ && f.progress >= 100) {
        setBusy(false);
        statusLabel_->setText(QStringLiteral("打磨完成，请取下工件并登记"));
        QMessageBox::information(this, QStringLiteral("完成"),
                                 QStringLiteral("本件打磨完成，请登记良品/不良品"));
    }
}

void OperatorWindow::onAlarm(const lx::AlarmRecord& a) {
    // 上游已按告警码边沿去重，此处只会收到状态变化
    statusBar()->showMessage(QStringLiteral("[%1] %2").arg(a.levelName(), a.text), 5000);
    if (a.level == lx::AlarmLevel::Critical) {
        setBusy(false);
        QMessageBox::critical(this, QStringLiteral("设备告警"), a.text);
    }
}

void OperatorWindow::onMarkGood() {
    ++totalCount_;
    ++goodCount_;
    updateCounter();
}

void OperatorWindow::onMarkReject() {
    ++totalCount_;
    updateCounter();
}

void OperatorWindow::updateCounter() {
    const double rate = totalCount_ > 0 ? 100.0 * goodCount_ / totalCount_ : 0.0;
    counterLabel_->setText(QStringLiteral("今日累计：%1 件　良品：%2 件　良率：%3%")
                               .arg(totalCount_).arg(goodCount_).arg(rate, 0, 'f', 1));
}

void OperatorWindow::closeEvent(QCloseEvent* e) {
    QSettings stats;
    stats.setValue(QStringLiteral("stats/total"), totalCount_);
    stats.setValue(QStringLiteral("stats/good"), goodCount_);
    QMainWindow::closeEvent(e);
}
