# ==============================================================================
#  burnishcore —— 灵犀智磨软件体系公共核心库（静态库）
#  被上位机（工程师版/操作工版）、算法工具链、云边网关共用
#  职责：通信协议 / 数据模型 / 工艺配置 / 日志 / 用户权限 / 遥测模拟器
#  注意：TEMPLATE/TARGET 必须在 include(common.pri) 之前定义，
#        common.pri 依据它们决定输出目录与是否链接核心库
# ==============================================================================

TARGET   = burnishcore
TEMPLATE = lib
CONFIG  += staticlib

include($$PWD/../../common.pri)

QT += core network

DESTDIR = $$LX_LIB_DIR

HEADERS += \
    $$PWD/protocol/FirmwareProtocol.h \
    $$PWD/model/ForceTorqueFrame.h \
    $$PWD/model/JointState.h \
    $$PWD/model/ProcessRecipe.h \
    $$PWD/model/AlarmRecord.h \
    $$PWD/comm/UdpControllerClient.h \
    $$PWD/comm/TelemetrySimulator.h \
    $$PWD/util/CsvLogger.h \
    $$PWD/util/UserManager.h

SOURCES += \
    $$PWD/model/ProcessRecipe.cpp \
    $$PWD/comm/UdpControllerClient.cpp \
    $$PWD/comm/TelemetrySimulator.cpp \
    $$PWD/util/CsvLogger.cpp \
    $$PWD/util/UserManager.cpp
