# ==============================================================================
#  VSCR-6EUR3-Monitor（工程师版）—— 机器人调试监控软件
#  在现有 VSCR-6EUR3-Monitor-V2.1.3 基础上迭代为完整工业上位机
#  五个功能模块以 .pri 组织，新增模块只需在此 include 一行
#  注意：TEMPLATE/TARGET 必须在 include(common.pri) 之前定义
# ==============================================================================

TARGET   = VSCR-6EUR3-Monitor-Engineer
TEMPLATE = app

# Qt Charts 为本模块硬依赖，缺失时给出明确提示而不是模糊报错
qtHaveModule(charts) {
    QT += charts
} else {
    error("缺少 Qt Charts 模块：请在 Qt 维护工具中安装 Qt Charts 后重新 qmake")
}

include($$PWD/../../common.pri)

QT += core gui widgets network

DESTDIR = $$LX_BIN_DIR

# —— 功能模块（与方案中的 5 个模块一一对应）——
include($$PWD/modules/monitoring/monitoring.pri)    # 1. 硬件状态监控
include($$PWD/modules/calibration/calibration.pri)  # 2. 标定工具
include($$PWD/modules/trajectory/trajectory.pri)    # 3. 轨迹编辑与仿真
include($$PWD/modules/tuning/tuning.pri)            # 4. 控制参数调参
include($$PWD/modules/task/task.pri)                # 5. 作业执行

SOURCES += $$PWD/main.cpp \
           $$PWD/MainWindow.cpp

HEADERS += $$PWD/MainWindow.h

# 统一工业风主题（QSS 编译进可执行文件，main.cpp 加载）
RESOURCES += $$PWD/monitor-engineer.qrc

# —— 可选第三方：OpenCASCADE（STEP 工件模型导入，用于轨迹编辑与仿真）——
# 安装 OCC 后取消以下注释即可启用，未启用时 STEP 导入功能自动给出提示
# LX_OCC_ROOT = C:/OpenCASCADE-7.7.0
# DEFINES += LX_HAVE_OCC
# INCLUDEPATH += $$LX_OCC_ROOT/inc
# LIBS += -L$$LX_OCC_ROOT/win64/vc14/lib -lTKernel -lTKTopAlgo -lTKSTEP -lTKGeomBase

# —— 可选第三方：OpenCV（视觉标定求解，用于标定工具模块）——
# 启用方式与 OCC 相同：
# DEFINES += LX_HAVE_OPENCV
# INCLUDEPATH += <opencv>/build/include
# LIBS += -L<opencv>/build/x64/vc16/lib -lopencv_calib3d4xx -lopencv_core4xx
