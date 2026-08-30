#include "TuningPanel.h"

#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

#include <cstring>

namespace {
const double kDefaultMass[6]     = {2.0, 2.0, 2.0, 0.05, 0.05, 0.05};
const double kDefaultDamping[6]  = {80.0, 80.0, 80.0, 2.0, 2.0, 2.0};
const double kDefaultStiffness[6] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
const char* kAxisNames[6] = {"X", "Y", "Z", "Rx", "Ry", "Rz"};
}  // namespace

TuningPanel::TuningPanel(lx::UdpControllerClient* ctrl, QWidget* parent)
    : QWidget(parent), ctrl_(ctrl) {
    auto* root = new QVBoxLayout(this);

    // —— 工艺标识 ——
    auto* idBox = new QGroupBox(QStringLiteral("工艺配置"), this);
    auto* idLay = new QFormLayout(idBox);
    recipeId_ = new QLineEdit(QStringLiteral("AL-PLATE-001"), this);
    recipeName_ = new QLineEdit(QStringLiteral("铝合金平板打磨"), this);
    idLay->addRow(QStringLiteral("工艺编号"), recipeId_);
    idLay->addRow(QStringLiteral("工艺名称"), recipeName_);
    root->addWidget(idBox);

    // —— 导纳控制矩阵 ——
    auto* matrices = new QHBoxLayout;
    matrices->addWidget(buildMatrixBox(QStringLiteral("惯性矩阵 M"), mass_, kDefaultMass));
    matrices->addWidget(buildMatrixBox(QStringLiteral("阻尼矩阵 D"), damping_, kDefaultDamping));
    matrices->addWidget(buildMatrixBox(QStringLiteral("刚度矩阵 K"), stiffness_, kDefaultStiffness));
    root->addLayout(matrices);

    // —— 打磨与控制参数 ——
    auto* proc = new QGroupBox(QStringLiteral("打磨与控制参数"), this);
    auto* g = new QGridLayout(proc);
    targetForce_ = new QDoubleSpinBox(this);
    targetForce_->setRange(0.0, 200.0);
    targetForce_->setValue(20.0);
    targetForce_->setSuffix(QStringLiteral(" N"));
    deadband_ = new QDoubleSpinBox(this);
    deadband_->setRange(0.0, 20.0);
    deadband_->setValue(0.5);
    deadband_->setSuffix(QStringLiteral(" N"));
    postureGain_ = new QDoubleSpinBox(this);
    postureGain_->setRange(0.0, 10.0);
    postureGain_->setValue(0.8);
    ctrlPeriod_ = new QDoubleSpinBox(this);
    ctrlPeriod_->setRange(1.0, 10.0);
    ctrlPeriod_->setValue(double(LX_CTRL_PERIOD_MS));
    ctrlPeriod_->setSuffix(QStringLiteral(" ms"));
    feedSpeed_ = new QDoubleSpinBox(this);
    feedSpeed_->setRange(0.5, 200.0);
    feedSpeed_->setValue(15.0);
    feedSpeed_->setSuffix(QStringLiteral(" mm/s"));
    spindleRpm_ = new QDoubleSpinBox(this);
    spindleRpm_->setRange(0.0, 20000.0);
    spindleRpm_->setValue(3000.0);
    spindleRpm_->setSuffix(QStringLiteral(" rpm"));

    g->addWidget(new QLabel(QStringLiteral("恒力目标"), this), 0, 0);
    g->addWidget(targetForce_, 0, 1);
    g->addWidget(new QLabel(QStringLiteral("力死区"), this), 0, 2);
    g->addWidget(deadband_, 0, 3);
    g->addWidget(new QLabel(QStringLiteral("姿态自适应增益"), this), 1, 0);
    g->addWidget(postureGain_, 1, 1);
    g->addWidget(new QLabel(QStringLiteral("控制器周期"), this), 1, 2);
    g->addWidget(ctrlPeriod_, 1, 3);
    g->addWidget(new QLabel(QStringLiteral("进给速度"), this), 2, 0);
    g->addWidget(feedSpeed_, 2, 1);
    g->addWidget(new QLabel(QStringLiteral("打磨头转速"), this), 2, 2);
    g->addWidget(spindleRpm_, 2, 3);
    root->addWidget(proc);

    auto* btns = new QHBoxLayout;
    auto* applyBtn = new QPushButton(QStringLiteral("下发到控制器"), this);
    auto* saveBtn = new QPushButton(QStringLiteral("保存工艺配置"), this);
    auto* loadBtn = new QPushButton(QStringLiteral("载入工艺配置"), this);
    btns->addStretch();
    btns->addWidget(applyBtn);
    btns->addWidget(saveBtn);
    btns->addWidget(loadBtn);
    root->addLayout(btns);
    root->addStretch();

    connect(applyBtn, &QPushButton::clicked, this, &TuningPanel::onApply);
    connect(saveBtn, &QPushButton::clicked, this, &TuningPanel::onSaveAs);
    connect(loadBtn, &QPushButton::clicked, this, &TuningPanel::onLoad);
}

QWidget* TuningPanel::buildMatrixBox(const QString& title, QDoubleSpinBox* out[6],
                                     const double def[6]) {
    auto* box = new QGroupBox(title, this);
    auto* f = new QFormLayout(box);
    for (int i = 0; i < 6; ++i) {
        out[i] = new QDoubleSpinBox(box);
        out[i]->setRange(0.0, 1.0e6);
        out[i]->setDecimals(4);
        out[i]->setValue(def[i]);
        f->addRow(QString::fromUtf8(kAxisNames[i]), out[i]);
    }
    return box;
}

void TuningPanel::syncFromUi() {
    recipe_.id = recipeId_->text();
    recipe_.name = recipeName_->text();
    recipe_.targetForceN = targetForce_->value();
    recipe_.feedSpeedMmS = feedSpeed_->value();
    recipe_.spindleRpm = spindleRpm_->value();
    recipe_.ctrlPeriodMs = ctrlPeriod_->value();
    recipe_.admittance.forceTarget = targetForce_->value();
    recipe_.admittance.forceDeadband = deadband_->value();
    recipe_.admittance.postureGain = postureGain_->value();

    // 对角矩阵写入 6x6 的第 i*6+i 位（工程上先调对角项，耦合项默认 0）
    auto fill = [](double m[36], QDoubleSpinBox* src[6]) {
        std::memset(m, 0, sizeof(double) * 36);
        for (int i = 0; i < 6; ++i) m[i * 6 + i] = src[i]->value();
    };
    fill(recipe_.admittance.mass, mass_);
    fill(recipe_.admittance.damping, damping_);
    fill(recipe_.admittance.stiffness, stiffness_);
}

void TuningPanel::syncToUi() {
    recipeId_->setText(recipe_.id);
    recipeName_->setText(recipe_.name);
    targetForce_->setValue(recipe_.targetForceN);
    feedSpeed_->setValue(recipe_.feedSpeedMmS);
    spindleRpm_->setValue(recipe_.spindleRpm);
    ctrlPeriod_->setValue(recipe_.ctrlPeriodMs);
    deadband_->setValue(recipe_.admittance.forceDeadband);
    postureGain_->setValue(recipe_.admittance.postureGain);

    auto fill = [](QDoubleSpinBox* dst[6], const double m[36]) {
        for (int i = 0; i < 6; ++i) dst[i]->setValue(m[i * 6 + i]);
    };
    fill(mass_, recipe_.admittance.mass);
    fill(damping_, recipe_.admittance.damping);
    fill(stiffness_, recipe_.admittance.stiffness);
}

void TuningPanel::onApply() {
    syncFromUi();
    QString err;
    if (!recipe_.isValid(&err)) {
        QMessageBox::warning(this, QStringLiteral("参数校验失败"), err);
        return;
    }
    if (!ctrl_->uploadAdmittance(recipe_.admittance, &err)) {
        QMessageBox::warning(this, QStringLiteral("下发失败"), err);
        return;
    }
    QMessageBox::information(this, QStringLiteral("完成"),
                             QStringLiteral("导纳参数已下发（目标恒力 %1 N）")
                                 .arg(recipe_.targetForceN));
}

void TuningPanel::onSaveAs() {
    syncFromUi();
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("保存工艺配置"),
        QStringLiteral("%1.json").arg(recipe_.id),
        QStringLiteral("工艺配置 (*.json)"));
    if (path.isEmpty()) return;

    QString err;
    if (!recipe_.isValid(&err)) {
        QMessageBox::warning(this, QStringLiteral("参数校验失败"), err);
        return;
    }
    if (!saveRecipeJson(recipe_, path, &err)) {
        QMessageBox::warning(this, QStringLiteral("保存失败"), err);
    }
}

void TuningPanel::onLoad() {
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("载入工艺配置"), QString(), QStringLiteral("工艺配置 (*.json)"));
    if (path.isEmpty()) return;

    QString err;
    if (!loadRecipeJson(recipe_, path, &err)) {
        QMessageBox::warning(this, QStringLiteral("载入失败"), err);
        return;
    }
    syncToUi();
}
