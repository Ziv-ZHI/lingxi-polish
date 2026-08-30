# 让其他 Agent 复现本工程的指令模板

下面三份指令按场景使用：A 一次性生成、B 分步生成（推荐，避免长输出截断）、C 只出方案不写代码。
复制时把「项目专有信息」段的数值替换成你自己的即可复用。

---

## A. 一次性生成（完整指令，可直接粘贴）

```text
你是一名工业软件架构师 + Qt 高级工程师。请为一个六轴协作打磨机器人项目
（灵犀智磨 BURNISH）生成一套**可编译的 Qt/qmake 工程骨架**，直接写入
<输出目录>/LingxiPolish/ 下，不要只给我代码片段。

【技术基线（硬约束，不得改动）】
- 构建系统：qmake（不是 CMake）。顶层用 TEMPLATE = subdirs，公共配置抽到 common.pri
- Qt 5.15 或 6.x，Windows 平台，C++17，源码 UTF-8（MSVC 加 /utf-8）
- 上位机沿用现有 VSCR-6EUR3-Monitor（Qt 前端），与 ARM Cortex 控制器走 UDP
- 控制器运行 Simulink MBD 自动生成的 .elf；EtherCAT 周期 2ms，接关节驱动器与
  KWR75 六维力传感器；视觉走 ROS 接口
- 输出目录统一 build/<debug|release>/{bin,lib}

【必须产出的目录结构】
LingxiPolish/
├── LingxiPolish.pro              # subdirs 顶层工程
├── common.pri                    # C++17/编码/版本号/输出目录/核心库链接
├── libs/burnishcore/             # 静态公共库
│   ├── protocol/FirmwareProtocol.h   # 与固件共享的协议，唯一源
│   ├── model/                        # 六维力、关节、工艺配置、告警 数据模型
│   ├── comm/UdpControllerClient.*    # 指令下发 + 遥测解析 + 丢包/超时检测
│   └── util/                         # CsvLogger、UserManager（权限分级）
├── apps/monitor-engineer/        # 工程师版上位机，5 个模块各一个 .pri
│   └── modules/{monitoring,calibration,trajectory,tuning,task}
├── apps/monitor-operator/        # 操作工版：选工艺→上料→一键打磨→完成登记
├── tools/{trajectory-planner,data-analyzer,process-db}
├── cloud/edge-gateway/           # MQTT/HTTP 上云，命令行参数可配
└── docs/README.md

【上位机工程师版 5 个模块的功能要求】
1. 状态监控：实时显示六维力/力矩、6 个关节的角度转速力矩、传感器温度、通讯状态；
   QtCharts 画力-时间、关节位置-时间曲线（5 秒滑动窗口，2ms 数据需降采样）；
   阈值越限告警 + 告警表 + CSV 日志落盘
2. 标定工具：相机内参/手眼/工作台标定参数管理与 yaml 导出；六维力零点标定与
   重力补偿——采集 6 组以上不同姿态，用最小二乘求负载质量与零偏
3. 轨迹编辑与仿真：自由/过渡/受限打磨三种模式；直线、圆弧、贝塞尔插补；
   导入 STEP 工件取表面点；用 DH 参数做正逆运动学（数值雅可比 + 阻尼最小二乘），
   仿真校验关节限位、奇异点（可操作度）、工作台碰撞，**仿真不通过禁止下发**
4. 调参面板：导纳控制 M/D/K 对角矩阵、恒力目标、力死区、控制周期、姿态自适应增益；
   参数存为工艺配置 JSON，可保存/载入/下发
5. 作业执行：启动/暂停/停止、进度条（进度由控制器上报）、末端运动预览

【协议契约（必须与固件一致，写在 protocol/FirmwareProtocol.h）】
遥测帧 TelemetryFrame 至少含：魔数、版本、帧序号、控制器时间戳、任务模式、
伺服模式、告警码、任务进度(0~100)、6 个关节的角/转速/力矩、六维力与力矩、
TCP 位姿、传感器温度、控制周期。
指令字：SetMode / SetAdmittance / SetForceTarget / LoadTrajectory /
Start / Pause / Stop / ZeroSensor / GravityComp。
UDP 端口：指令 5001，遥测 5002。

【代码规范】
- 每个含 Q_OBJECT 的头文件都必须写进 .pro 的 HEADERS，否则 moc 不生成会链接失败
- 可选第三方依赖（OpenCV 视觉标定、OpenCASCADE 的 STEP、QtMqtt）用
  `DEFINES += LX_HAVE_XXX` 条件编译，未启用时给出提示而不是编译失败
- 权限三级：操作工仅执行、工程师改参数、管理员管账号，密码存 SHA-256 不存明文
- 中文注释，注释说明"为什么"；不写投机性代码，不做超出需求的抽象
- 每个可执行模块要能真正编译通过（有 main.cpp，不是空壳）

【边界（不要生成）】
下位机固件由 Simulink 生成，不属本工程；微信小程序与云端 SaaS 后台是
Web/JS 技术栈，不纳入 qmake；移动端不做实时控制。这三类在 README 里说明边界即可。

【交付验收】
1. qmake LingxiPolish.pro 能解析并生成 Makefile
2. 所有 .pro/.pri 中列出的文件真实存在
3. docs/README.md 说明：目录结构、五大类软件与工程的映射表、构建方式、
   可选依赖启用方法、与固件的协议约定、后续开发顺序
4. 最后自查一遍再交付，并列出已知边界
```

---

## B. 分步生成（推荐：长工程一次生成容易截断）

先发「A 指令」的【技术基线】+【目录结构】两段作为公共上下文，然后按序追加：

```text
第 1 步：只生成 common.pri、顶层 LingxiPolish.pro、libs/burnishcore 全部内容
        （协议、数据模型、UDP 客户端、日志、权限）。生成后等我确认。
第 2 步：生成 apps/monitor-engineer 的 monitoring + task 两个模块和主窗口。
第 3 步：生成 calibration + trajectory + tuning 三个模块。
第 4 步：生成 apps/monitor-operator。
第 5 步：生成 tools 三个工具。
第 6 步：生成 cloud/edge-gateway 与 docs/README.md。
```

每步结尾固定加一句约束，能显著提高质量：

```text
约束：只写本步列出的文件，不要改动前面已确认的文件；
     新增文件要能被上一步的 .pro 正确收录；完成后列出你创建的文件清单。
```

---

## C. 只出方案不写代码

```text
基于上述技术基线，先不要写代码。输出：
1. 模块划分与依赖关系图（文字版树状结构即可）
2. 每个模块的职责、输入输出、关键数据结构
3. 上位机与固件的通信协议字段表（字段名/类型/含义）
4. 开发顺序与每步的可验证标准
确认无误后我再让你逐步写代码。
```

---

## 让指令更有效的 5 个技巧

1. **先给硬约束再给功能**：Qt 版本、构建系统、通信周期这类写死在前面，
   否则模型会自作主张改用 CMake 或虚构协议
2. **明确"可编译"而不是"示例"**：加一句"有 main.cpp，不是空壳"，能避免生成一堆伪代码
3. **要求列出文件清单**：便于你核对是否漏文件，也便于发现它偷偷改了别处
4. **要它自查**：末尾加"完成后自查并列出已知边界"，比事后你审更快
5. **长工程分批发**：一次超过约 30 个文件，输出质量和完整性都会明显下降
