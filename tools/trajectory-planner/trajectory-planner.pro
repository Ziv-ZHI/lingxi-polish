# ==============================================================================
#  算法工具链 1：轨迹规划工具
#  输入工件点云（CSV: x,y,z），自动生成复杂曲面打磨轨迹（扫描线路径），
#  输出轨迹点文件（CSV）供上位机轨迹模块与控制器使用
#  注意：TEMPLATE/TARGET 必须在 include(common.pri) 之前定义
# ==============================================================================

TARGET   = TrajectoryPlanner
TEMPLATE = app

include($$PWD/../../common.pri)

QT += core gui widgets

DESTDIR = $$LX_BIN_DIR

SOURCES += $$PWD/main.cpp

# 启用 STEP 模型输入（需要 OpenCASCADE）：
# DEFINES += LX_HAVE_OCC
# INCLUDEPATH += <occ>/inc
# LIBS += -L<occ>/win64/vc14/lib -lTKernel -lTKTopAlgo -lTKSTEP
