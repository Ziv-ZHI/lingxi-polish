# ==============================================================================
#  算法工具链 2：实验数据分析软件
#  导入打磨实验日志，自动计算力跟踪误差与轨迹跟踪误差，
#  生成对比图表，支持"有无鲁棒控制器"两组实验对照，辅助论文与算法迭代
#  注意：TEMPLATE/TARGET 必须在 include(common.pri) 之前定义
# ==============================================================================

TARGET   = DataAnalyzer
TEMPLATE = app

qtHaveModule(charts) {
    QT += charts
} else {
    error("缺少 Qt Charts 模块：请在 Qt 维护工具中安装 Qt Charts 后重新 qmake")
}

include($$PWD/../../common.pri)

QT += core gui widgets

DESTDIR = $$LX_BIN_DIR

SOURCES += $$PWD/main.cpp
