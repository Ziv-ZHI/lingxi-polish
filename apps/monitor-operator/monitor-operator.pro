# ==============================================================================
#  VSCR-6EUR3-Monitor（操作工版）—— 工厂生产作业软件
#  面向产线工人：隐藏调参功能，仅保留 选工艺 -> 上料 -> 一键打磨 -> 完成登记
#  权限由 burnishcore 的 UserManager 控制：操作工仅可执行任务
#  注意：TEMPLATE/TARGET 必须在 include(common.pri) 之前定义
# ==============================================================================

TARGET   = VSCR-6EUR3-Monitor-Operator
TEMPLATE = app

include($$PWD/../../common.pri)

QT += core gui widgets network

DESTDIR = $$LX_BIN_DIR

SOURCES += $$PWD/main.cpp \
           $$PWD/OperatorWindow.cpp

HEADERS += $$PWD/OperatorWindow.h

# 统一工业风主题（QSS 编译进可执行文件，main.cpp 加载）
RESOURCES += $$PWD/monitor-operator.qrc
