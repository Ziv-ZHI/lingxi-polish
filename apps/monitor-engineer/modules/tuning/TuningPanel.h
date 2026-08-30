// 模块 4：控制参数调参面板
//   导纳控制 M/D/K 矩阵（对角形式：平动 xyz + 转动 xyz）
//   恒力目标值、控制周期、姿态自适应参数；参数方案存为工艺配置文件
#pragma once

#include <QWidget>

#include "comm/UdpControllerClient.h"
#include "model/ProcessRecipe.h"

QT_BEGIN_NAMESPACE
class QDoubleSpinBox;
class QLineEdit;
class QPushButton;
QT_END_NAMESPACE

class TuningPanel : public QWidget {
    Q_OBJECT
public:
    explicit TuningPanel(lx::UdpControllerClient* ctrl, QWidget* parent = nullptr);

    const lx::ProcessRecipe& recipe() const { return recipe_; }

private slots:
    void onApply();     // 下发到控制器
    void onSaveAs();    // 保存工艺配置
    void onLoad();      // 载入工艺配置

private:
    QWidget* buildMatrixBox(const QString& title, QDoubleSpinBox* out[6], const double def[6]);
    void syncFromUi();
    void syncToUi();

    lx::UdpControllerClient* ctrl_ = nullptr;
    lx::ProcessRecipe recipe_;

    QLineEdit* recipeId_ = nullptr;
    QLineEdit* recipeName_ = nullptr;
    QDoubleSpinBox* mass_[6] = {};
    QDoubleSpinBox* damping_[6] = {};
    QDoubleSpinBox* stiffness_[6] = {};
    QDoubleSpinBox* targetForce_ = nullptr;
    QDoubleSpinBox* deadband_ = nullptr;
    QDoubleSpinBox* postureGain_ = nullptr;
    QDoubleSpinBox* ctrlPeriod_ = nullptr;
    QDoubleSpinBox* feedSpeed_ = nullptr;
    QDoubleSpinBox* spindleRpm_ = nullptr;
};
