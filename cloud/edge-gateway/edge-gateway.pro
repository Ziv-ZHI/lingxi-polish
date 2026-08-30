# ==============================================================================
#  云端平台（边侧）：工业云边网关
#  现场工控机常驻进程，采集控制器遥测并上云（MQTT/HTTP），同时接收云端指令
#  对应云端能力：设备云监控 / 工艺云库 / 预测性维护 / 良率报表
#  命令行参数：--device-id <ID> [--broker host] [--port 1883] [--interval 1000]
#  注意：TEMPLATE/TARGET 必须在 include(common.pri) 之前定义
# ==============================================================================

TARGET   = EdgeGateway
TEMPLATE = app

include($$PWD/../../common.pri)

QT += core network

# Qt MQTT 模块可选：安装后自动启用；未安装时退化为 HTTP 上报模式
qtHaveModule(mqtt) {
    QT += mqtt
    DEFINES += LX_HAVE_MQTT
}

CONFIG += console
CONFIG -= app_bundle

DESTDIR = $$LX_BIN_DIR

SOURCES += $$PWD/main.cpp
