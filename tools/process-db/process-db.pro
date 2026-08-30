# ==============================================================================
#  算法工具链 3：工艺数据库管理工具
#  按 材料 / 曲面 / 打磨头 建立最优恒力、速度、轨迹参数库，
#  新工件直接检索复用，减少现场调试时间；可导出为上位机工艺配置 JSON
#  注意：TEMPLATE/TARGET 必须在 include(common.pri) 之前定义
# ==============================================================================

TARGET   = ProcessDB
TEMPLATE = app

include($$PWD/../../common.pri)

QT += core gui widgets sql

DESTDIR = $$LX_BIN_DIR

SOURCES += $$PWD/main.cpp
