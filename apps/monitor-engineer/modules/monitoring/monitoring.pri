# 模块 1：硬件状态监控
#   实时读取六维力/力矩、关节角度/转速/力矩、控制器状态、传感器温度、通讯状态
#   绘制 力-时间、位置-时间曲线；阈值越限弹窗告警 + 日志
HEADERS += $$PWD/MonitoringPanel.h
SOURCES += $$PWD/MonitoringPanel.cpp
