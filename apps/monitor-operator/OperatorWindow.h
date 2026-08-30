// 操作工版主窗口：极简三步式作业流程（选工艺 -> 上料确认 -> 一键打磨 -> 完成登记）
#pragma once

#include <QMainWindow>

#include "comm/TelemetrySimulator.h"
#include "comm/UdpControllerClient.h"
#include "model/ProcessRecipe.h"
#include "util/UserManager.h"

QT_BEGIN_NAMESPACE
class QCheckBox;
class QCloseEvent;
class QComboBox;
class QLabel;
class QProgressBar;
class QPushButton;
QT_END_NAMESPACE

class OperatorWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit OperatorWindow(lx::UserManager& users, QWidget* parent = nullptr);

private slots:
    void onRecipeChanged(int index);
    void onExecute();
    void onStop();
    void onTelemetry(const lx::protocol::TelemetryFrame& f);
    void onAlarm(const lx::AlarmRecord& a);
    void onMarkGood();
    void onMarkReject();

private:
    void buildUi();
    void loadRecipes(const QString& dir);
    void updateCounter();
    void updateExecEnabled();   // 启动条件：已选工艺 + 已上料确认 + 非忙碌
    void setBusy(bool busy);

protected:
    void closeEvent(QCloseEvent* e) override;

    lx::UserManager& users_;
    lx::UdpControllerClient ctrl_;
    lx::TelemetrySimulator sim_;
    QVector<lx::ProcessRecipe> recipes_;
    QString recipesDir_;

    QComboBox* recipeCombo_ = nullptr;
    QLabel* recipeInfo_ = nullptr;
    QCheckBox* loadCheck_ = nullptr;      // 上料确认：未确认不允许启动
    QPushButton* execBtn_ = nullptr;
    QPushButton* stopBtn_ = nullptr;
    QPushButton* goodBtn_ = nullptr;
    QPushButton* rejectBtn_ = nullptr;
    QProgressBar* progress_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QLabel* counterLabel_ = nullptr;
    QLabel* forceLabel_ = nullptr;
    QCheckBox* simCheck_ = nullptr;       // 模拟遥测（无控制器开发用）

    int totalCount_ = 0;
    int goodCount_ = 0;
    bool busy_ = false;
};
