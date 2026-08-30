# ==============================================================================
#  灵犀智磨 LingxiPolish —— 协作打磨机器人配套软件体系
#  顶层 qmake 工程（subdirs），用 Qt Creator 打开本文件即可加载全部子工程
#
#  技术基线（与项目硬件/现有成果一致）：
#    - 上位机沿用 VSCR-6EUR3-Monitor，Qt 框架，Windows 平台
#    - 控制器：ARM Cortex，运行 Simulink MBD 自动生成的 .elf
#    - 通信：控制器-驱动器/六维力用 EtherCAT（2ms 周期）
#            上位机-控制器用 UDP；视觉用 ROS 接口
#    - 传感器：KWR75 六维力传感器
#
#  五大类软件与本工程的对应关系：
#    1. PC 上位机（工程师版 / 操作工版） -> apps/monitor-engineer、apps/monitor-operator
#    2. 算法工具链                     -> tools/trajectory-planner、tools/data-analyzer、tools/process-db
#    3. 下位机嵌入式固件               -> 由 Simulink/Embedded Coder 生成，不属于 qmake 管理；
#                                        与上位机共享的协议定义在 libs/burnishcore/protocol
#    4. 云端平台                       -> cloud/edge-gateway（边侧 Qt 网关）
#                                        云端 SaaS 后台与微信小程序为 Web/JS 技术栈，
#                                        见 docs/README.md，不纳入本 qmake 构建
#    5. 微信小程序                     -> 微信原生/uni-app，见 docs/README.md
#
#  公共库 libs/burnishcore 提供：通信、数据模型、工艺配置、日志、权限
# ==============================================================================

TEMPLATE = subdirs
CONFIG  += ordered          # 保证 burnishcore 先于各应用构建

SUBDIRS += \
    libs/burnishcore \
    apps/monitor-engineer \
    apps/monitor-operator \
    tools/trajectory-planner \
    tools/data-analyzer \
    tools/process-db \
    cloud/edge-gateway

# 依赖顺序由 CONFIG += ordered 与上面的 SUBDIRS 顺序保证

OTHER_FILES += \
    $$PWD/common.pri \
    $$PWD/docs/README.md
