# 灵犀智磨（BURNISH）配套软件体系 — Qt 工程说明

本目录是"协作打磨机器人配套软件方案"的 **Qt/qmake 工程骨架**，与项目现有成果对齐：

- 上位机沿用 `VSCR-6EUR3-Monitor`，Qt 框架开发，Windows 平台
- 控制器为 ARM Cortex，运行 Simulink MBD 自动生成的 `.elf`
- 控制器 ↔ 关节驱动器/KWR75 六维力传感器：EtherCAT（2 ms 周期）
- 上位机 ↔ 控制器：UDP（指令 5001 / 遥测 5002）
- 视觉：ROS 接口；标定结果以 yaml 共享

本工程已在 **Qt 5.15.2 (MinGW 8.1, Windows x64)** 下全量编译通过（`build/release/bin`
共 7 个目标）；代码同时兼容 Qt 6.x（QtCharts 命名空间差异已用 `QT_VERSION` 宏隔离）。

---

## 一、目录结构

```
LingxiPolish/
├── LingxiPolish.pro              # 顶层 subdirs 工程（Qt Creator 打开此文件）
├── common.pri                    # 全局配置：C++17、中文编码、版本号、输出目录、核心库链接
├── libs/burnishcore/             # 公共核心库（静态库，所有应用共用）
│   ├── protocol/                 #   FirmwareProtocol.h：与下位机共享的协议（唯一源，无 Qt 依赖）
│   ├── model/                    #   六维力/关节/工艺配置/告警 数据模型
│   ├── comm/                     #   UDP 控制器客户端 + 遥测模拟器（指令下发/遥测解析/丢包超时）
│   └── util/                     #   CSV 日志、用户权限分级（SHA-256）
├── apps/
│   ├── monitor-engineer/         # 上位机工程师版（5 个功能模块，各以 .pri 组织）
│   │   └── modules/{monitoring, calibration, trajectory, tuning, task}
│   └── monitor-operator/         # 上位机操作工版（选工艺→上料→一键打磨→完成登记）
├── tools/
│   ├── trajectory-planner/       # 轨迹规划工具（点云 → 扫描线轨迹）
│   ├── data-analyzer/            # 实验数据分析（力跟踪误差指标 + 有无鲁棒控制器双组对照）
│   └── process-db/               # 工艺数据库（SQLite，检索复用工艺参数）
├── cloud/edge-gateway/           # 云边网关（遥测聚合上云 MQTT/HTTP + 云端指令下行）
├── demo/monitor-demo.html        # 零依赖 HTML 演示页（无需 Qt 环境即可演示交互逻辑）
└── docs/README.md
```

构建产物统一输出到 `build/<debug|release>/bin`（可执行文件）与
`build/<debug|release>/lib`（burnishcore.a / .lib）。

---

## 二、五大类软件方案 ↔ 工程模块映射

| 方案分类 | 工程模块 | 说明 |
|---|---|---|
| 一、PC 上位机（工程师版） | `apps/monitor-engineer` | 状态监控 / 标定工具 / 轨迹编辑与仿真 / 参数调参 / 作业执行 |
| 一、PC 上位机（操作工版） | `apps/monitor-operator` | 选工艺 → 上料确认 → 一键打磨 → 完成登记，隐藏调参 |
| 二、微信小程序 | 不在本工程（微信原生 / uni-app） | 建议独立目录 `miniprogram/`，对外客户小程序 + 对内运维小程序 |
| 三、下位机嵌入式固件 | 不在本工程（Simulink/Embedded Coder 生成） | 与上位机共享的协议定义在 `libs/burnishcore/protocol` |
| 四、算法工具链 | `tools/*` | 轨迹规划 / 数据分析 / 工艺数据库 |
| 五、云平台 | `cloud/edge-gateway`（边侧） | 云端 SaaS 后台为 Web 技术栈，建议独立目录 `cloud/server/` |

> **边界说明**：移动端不做实时运动控制，只做查看与工单业务；实时控制由 PC 上位机与下位机承担。

---

## 三、构建方式

### 环境要求

| 项 | 要求 |
|---|---|
| Qt | 5.15 或 6.x（已验证：5.15.2 + MinGW 8.1 / Windows x64） |
| 必装组件 | Qt Core / GUI / Widgets / Network / **Charts** |
| 可选组件 | Qt SQL（工艺库工具）、Qt MQTT（云边网关，装了自动启用） |
| 编译器 | MSVC 2019/2022 或 MinGW（MSVC 路径自动加 `/utf-8`，源码均为 UTF-8） |

### 命令行构建（MinGW 示例）

```bat
set PATH=C:\Qt\5.15.2\mingw81_64\bin;C:\Qt\Tools\mingw810_64\bin;%PATH%
cd LingxiPolish
mkdir build\shadow && cd build\shadow
qmake ..\..\LingxiPolish.pro "CONFIG+=release"
mingw32-make -j8
```

MSVC 环境在"Qt 命令行（MSVC）"下执行相同命令，`make` 换成 `nmake` 或 `jom`。
Qt Creator 直接打开 `LingxiPolish.pro` → 配置 Kit → 构建即可。

### 可选依赖启用方式

| 依赖 | 宏 | 启用方法 | 未启用时的行为 |
|---|---|---|---|
| OpenCASCADE | `LX_HAVE_OCC` | 在 `monitor-engineer.pro` / `trajectory-planner.pro` 取消注释 OCC 配置段 | STEP 导入按钮给出提示，可改用点云 CSV 流程 |
| OpenCV | `LX_HAVE_OPENCV` | 在 `monitor-engineer.pro` 取消注释 OpenCV 配置段 | 标定面板仅做内参/手眼/工作台参数管理与 yaml 导出 |
| Qt MQTT | `LX_HAVE_MQTT` | 安装 QtMqtt 模块后 `edge-gateway.pro` 的 `qtHaveModule(mqtt)` 自动启用 | 网关只走 HTTP 上报，控制台给出提示 |

所有可选依赖都走条件编译：未启用只是功能降级并提示，不影响编译。

---

## 四、与固件的协议约定（唯一源：libs/burnishcore/protocol/FirmwareProtocol.h）

该头文件**只依赖 `<cstdint>`**，可被固件侧直接 `#include`；结构体 1 字节对齐，
小端字节序，并用 `static_assert` 锁住布局（改字段编译即报错，防止两边悄悄错位）。

### 遥测帧 TelemetryFrame（控制器 → 上位机，5002，2ms 一帧，268 字节）

| 字段 | 类型 | 说明 |
|---|---|---|
| magic / version | u32 / u16 | `0x4C58504F`("LXPO") / 协议版本 2 |
| seq | u16 | 帧序号，上位机据此统计丢包（16 位回绕） |
| timestampUs | u64 | 控制器时间戳（μs） |
| mode / servoMode | u8 | TaskMode（自由/过渡/打磨/回零/标定/急停）、ServoMode（CSP/CST/CSV） |
| alarmCode | u8 | 告警码，0=无（上位机按边沿触发告警，恢复时补提示） |
| progress | u8 | 任务进度 0~100，作业页/操作工版进度条的数据源 |
| jointPos/Vel/Tor[6] | double×18 | 关节角 rad、角速度 rad/s、力矩 N·m |
| force[3] / torque[3] | double×6 | 六维力 N、六维力矩 N·m |
| tcpPose[6] | double×6 | 末端 x y z（mm）+ rx ry rz（rad，ZYX 欧拉） |
| sensorTempC / ctrlPeriodMs | float×2 | 传感器温度 ℃ / 实际控制周期 ms |

### 指令帧（上位机 → 控制器，5001）

统一布局：`u32 魔数 | u16 指令码 | u32 载荷长度 | 载荷`。

| 指令码 | 载荷 |
|---|---|
| SetMode (0x0101) | TaskMode 1 字节 |
| SetAdmittance (0x0102) | `AdmittanceParams` 888 字节（M/D/K 3×36 + 目标力/死区/自适应增益） |
| SetForceTarget (0x0103) | `ForceTargetPayload` 16 字节（targetN, deadbandN） |
| LoadTrajectory (0x0104) | `int32 点数` + 逐点 `double[6]`（x,y,z,rx,ry,rz；mm/rad），与轨迹 CSV 列序一致 |
| Start / Pause / Stop (0x0201~0203) | 无载荷 |
| ZeroSensor (0x0301) | 无载荷 |
| GravityComp (0x0302) | `GravityCompPayload` 56 字节（massKg + 力零偏[3] + 力矩零偏[3]） |

固件侧对应实现为 `linuxUDP.c`，改协议必须同步两边并递增 `kVersion`。

---

## 五、默认账号与权限

- 权限三级：**操作工**（仅执行）/ **工程师**（改参数）/ **管理员**（管账号），
  密码以 SHA-256（用户名作盐）存储于 `%APPDATA%/BURNISH-LingxiPolish/accounts.json`
- 首次运行自动创建管理员：**用户名 `admin`，密码 `lingxi@2026`**，部署后请立即修改
  密码（工程骨架未提供改密界面，见"当前边界"）
- 操作工版允许操作工/管理员登录；工程师版要求工程师及以上权限

---

## 六、无真机开发：遥测模拟器

真实控制器不在线时，可在两个上位机中勾选 **"模拟遥测"**：
`libs/burnishcore/comm/TelemetrySimulator` 按 2ms 生成合成遥测帧（往复扫描轨迹、
恒力一阶收敛、关节摆动、30 秒循环进度），经 `UdpControllerClient::injectFrame`
走与网络帧**完全相同**的解析分发路径，曲线、告警、进度、预览全部可联调。
停止模拟 200ms 后会触发"通讯中断"告警，这本身就是超时检测功能的演示。

操作工版首次运行会在工艺目录生成两份示例工艺 + 演示轨迹 CSV，保证"一键打磨"
全链路可点通。

---

## 七、软件产品组合（与商业模式画布对应）

| 交付形态 | 包含模块 | 收费方式 |
|---|---|---|
| 标配（随硬件附赠） | 下位机固件 + 操作工版上位机 + 客户小程序 | 免费 |
| 付费增值（年订阅） | 工程师版上位机 + 工艺数据库工具 + 云平台账号 | 按年订阅 |
| 定制开发（项目制） | 对接客户 MES/产线的二次开发 | 项目服务费 |

权限分级（`util/UserManager`）已在代码层面区分**操作工 / 工程师 / 管理员**三级。

---

## 八、建议开发顺序

1. `burnishcore` 协议与通信跑通（与现有固件联调，先做只读遥测）
2. 工程师版模块 1（状态监控）—— 验证曲线与告警
3. 模块 4（调参）+ 模块 5（作业执行）—— 打通参数下发与任务闭环
4. 模块 2（标定）、模块 3（轨迹与仿真）—— 依赖相机/OCC，可并行
5. 操作工版 —— 复用核心库，界面最简
6. `tools/*` 与 `cloud/edge-gateway` —— 增值能力，可后补

---

## 九、可视化演示 Demo（无需 Qt 环境）

`demo/monitor-demo.html` —— 单文件、零依赖，浏览器双击即可打开。

用途：**iCAN 答辩截图 / 录屏素材**，以及在没有真机、没有 Qt 环境时验证交互逻辑。
模拟内容与演示脚本（启动 → 力收敛 → 关闭鲁棒控制器看波动 → 改目标力看跟随 →
通讯中断告警 → 停止）见页面内说明。

### 9.1 设计系统（视觉语言）

参考 Dieter Rams「零装饰 · 功能即美」与 Teenage Engineering 工业器械质感，
避开常见"AI 生成页面"的视觉指纹（系统默认字体栈、统一大圆角、卡片通用投影、
渐变进度条、紫蓝渐变配色、iOS 胶囊开关）。

| 维度 | 决策 |
|------|------|
| 品牌色 | 琥珀 `#E5A03C`（深色）/ `#7A4A12`（浅色）—— 打磨火花 + 工业警示语义 |
| 辅色 | 钢青 `#5C93A6` / `#2E5C6E` —— 第二数据序列、目标/设定值线 |
| 语义色 | 正常绿 `#66C482` / `#2A7340`；注意 `#EBD23A` / `#6B6E00`；报警 `#EE5F42` / `#A8381F` |
| 底色 | 深色近黑 `#0D1013`；浅色暖白纸感 `#EFEDE8`（非冷白） |
| 形状 | 圆角收敛到 2px；卡片用 1px 发丝线 + 顶部内高光，**不用投影**（手机外壳除外，它是"实物"） |
| 字体 | 三档分工：黑体做骨架 / 楷体做点睛 / 等宽做读数（详见 9.3） |
| 底纹 | 工作区铺 8px 细栅格 + 72px 主栅格（图纸感），随内容滚动 |
| 开关 | 方形滑动开关（32×16，位移 16px），非 iOS 胶囊 |
| 动效 | 110ms 线性缓动；`prefers-reduced-motion` 下全部关闭 |

**可访问性**：双主题共 28 组前景/背景组合实测对比度 **均 ≥ 4.5:1**
（深色最低 5.14，浅色最低 4.60），满足 WCAG AA 正文要求；语义色之间色相间隔
≥ 30°（浅色 11°/32°/62°/138°/197°），避免图表里"红橙不分"。

### 9.2 回归验证

```bash
python demo/_verify.py     # 静态：id 契约 / 视图导航 / canvas 覆盖 / 函数定义
node   demo/_smoke.js      # 冒烟 + 数值：8 视图切换、13 画布绘制、标定与重力补偿解算
```

`_smoke.js` 采用**两层判据**：单次试验只拦粗差（|Δm|<0.15 kg、|Δb|<0.25 N），
无偏性用 6 次独立试验的均值判定（|E[Δm]|<0.02 kg、|E[Δb]|<0.05 N），
并用"残差 ≈ 注入噪声 σ"验证最小二乘无偏。

建议连跑 3 轮确认不 flaky（随机噪声下有界即可，不要求数值一致）。

### 9.3 字体系统（黑体骨架 + 楷体点睛 + 等宽读数）

中文界面若全用系统默认黑体，容易落入"AI 生成页面"的视觉指纹。本 demo 采用
**三档分工**：黑体承担结构，楷体承担气质，等宽承担精度。

| 档位 | 变量 | 用途 | 约束 |
|------|------|------|------|
| 黑体（骨架） | `--ff-ui` | 正文、标签、控件、数据行、图例 | 系统字体栈优先（PingFang SC / HarmonyOS Sans SC / 微软雅黑），不引入 Web 字体 |
| 楷体（点睛） | `--ff-kai` | 品牌名、视图标题、卡片头、小程序导航标题 / 设备名 / 用户名、空状态提示语 | **≥13px**、**绝不加粗**、仅限短标题与提示语 |
| 等宽（读数） | `--ff-num` | 一切数值、单位、坐标轴刻度 | 强制 `tabular-nums` + 斜杠零，防数字跳动 |

楷体栈顺序：`STKaiti → Kaiti SC → Kaiti TC → KaiTi → 楷体 → AR PL UKai CN → --ff-ui`
（华文楷体字面质量优于 `simkai`，故前置；末位回退保证 Linux 下不塌）。

**三条硬约束（踩过坑，勿破）**：

1. **楷体不加粗** —— 楷体无真正的粗体字面，浏览器会做劣化伪粗，笔画糊成一团。
   所有楷体元素统一 `font-weight:500`。
2. **楷体字宽比黑体宽 10–15%** —— 同样字号在窄栏里更容易撑栏换行。
   放大字号后必须回退字距（本工程卡片头从 14px + `.05em` 回调到 13.5px + `.02em`）。
3. **画布内字体需显式注入** —— Canvas `ctx.font` 不继承 CSS 变量，JS 侧定义
   `FONT_CN` 常量统一替换裸 `sans-serif`；中文低于 **10px** 辨识困难，小程序画布
   已从 9.5px 提到 10px。

### 9.4 小程序端与 WeUI 对齐

小程序预览页（视图 ID `mini`）的对标基线是微信官方 `Tencent/weui-wxss`，
只取它的**度量与层级语义**，不套它的组件样式。

- **层级**：`--mp-bg #F2F0EA`（WeUI BG-0 页面底）/ `--mp-card #FFFFFF`（BG-2 卡片）
- **文字**：沿用 WeUI 的"**墨色 + 不透明度**"而非固定灰阶 ——
  `rgba(21,23,25,.92 / .72 / .62)` 对应 FG-0/1/2。同一变量在页面底与卡片上
  自动协调，这是 WeUI 最值得借鉴的一点。
  辅助色已由 WeUI 原生的 `.3` 抬到 `.62`，以满足 10px 小字的 4.5:1。
- **分隔线**：WeUI 的**内缩分隔线**（从文字左缘起 `left:14px; right:0`），
  不是通栏横线 —— 列表信息密度立刻上一个台阶。
- **控件度量**：主按钮 48px / 中按钮 40px / 小按钮 32px，圆角 8px；
  cell 高 56px、正文 17px；TabBar 高 56px。
- **品牌绿拆两档**（重要取舍）：
  `--mp-brand #07C160` **只用于图形**（图表、图标、填充块，不受文字对比度约束）；
  `--mp-brand-ink #0A854B` 用于**一切承载文字的绿**。
  原因是微信绿配白字实测仅 **2.38:1**，远低于 AA 4.5，直接用会不可读。

小程序端前景/背景实测对比度：卡片白底 14.42 / 7.13 / 5.04，
页面底 12.95 / 6.67 / 4.82 —— 全部 ≥ 4.5:1。

### 9.5 小程序端交互模型（Tab 内联，已验证）

`mini` 视图不是静态预览，而是**真实驱动主仿真（monitor 视图那套状态机）**的可交互端。
它本质上是"网页在手机上的样式"——**5 个一级 Tab 直接切换、内容全部内联展示，
没有右滑二级页、没有动作面板 / 二次确认弹层 / 遮罩**。这样在真实浏览器里不会出现
覆盖层不显示、页面切不动的问题（早期"深度交互"版用 `.mp-pane/.mp-sheet/.mp-dlg/.mp-mask`
覆盖层，在部分浏览器渲染失败导致"页面不会转换"，已废弃）。

所有写操作仍遵循"移动端不直控"原则：先经 `mpSend` 下发、显示"等待回执"指示条、
回执到达后才代理主按钮改变界面，避免移动端绕过 2ms 实时闭环。

- **5 个一级 Tab**：`home`（设备切换 + 实时力 hero + 快捷操作 + 待处理事项 + 告警内联展开）/
  `dev`（设备列表，点击任一项**就地切换**选中态，不进二级页）/
  `data`（周期分段 + 近 1/7/30 日柱状图 + 质量分布环形图，点柱看明细）/
  `order`（工单状态机 `todo → doing → check → done`，**点击卡片就地展开**流转记录与状态按钮 + 筛选 + 人工建单）/
  `me`（开关 / 阈值循环切换 / 权限申请 / CSV 导出 / 关于卡片内联 / 清除缓存）。
- **内联而非下钻**：设备列表点击 = 切换当前设备；告警卡片点击 = 就地展开建议处置步骤 +
  「转为工单」；工单卡片点击 = 就地展开流转记录 + 接单/提交验收/验收通过。
- **合理性约束**：离线设备（`#03`）与未接入本产线网关的设备（`#02`）按钮置灰并给出
  不同原因文案；危险操作（停止 / 急停）**直接下发**并显示"等待回执"，不再走二次确认弹层；
  操作工权限不得改工艺参数（改工艺走工程师/管理员回执提示）。
- **原生交互组件**：仅保留 `Toast`（轻提示），与微信原生体验对齐；`ActionSheet`/`Dialog`/
  右滑 `SubPage` 已移除。
- **回归验证**：`demo/_verify.py`（静态契约，已删除二级页 / 原生组件契约）与
  `demo/_smoke.js`（Node + DOM/Canvas stub 真实执行 JS，含重力补偿最小二乘数值验证）
  双保险；`mpSend` 回执为 `setTimeout`，测试须 `flushTimers()` 推进，盲点"全部按钮"时
  **不得**点 `btnLink/btnStart/btnPause/btnStop`（其副作用会经 `renderMp` 把链路断开/机器运行
  传播到移动端，导致后续用例误报）。

---

## 十、当前边界（后续扩展点）

- **改密界面**：账号数据层支持改密，骨架未提供改密 UI；首次部署后请直接编辑
  `accounts.json` 重新生成 SHA-256
- **数据分析**：已实现力跟踪误差指标与双组对照；**轨迹跟踪误差**需导入参考轨迹
  CSV 后扩展（运行日志已记录 TCP 位姿，接口位置：`tools/data-analyzer/main.cpp`）
- **视觉标定求解**：启用 OpenCV 后需在 `CalibrationPanel::onCompute` 接入
  `cv::calibrateCamera / calibrateHandEye` 读图求解流程；未启用时参数管理与
  yaml 导出可用
- **STEP 表面取点**：启用 OpenCASCADE 后需在 `TrajectoryPanel::onImportStep`
  补充面片采样生成路径点的逻辑
- **重力补偿模型**：按"末端 z 轴方向重力投影 + 常值零偏"建模，未含工具重心偏置
  引起的力矩项（力矩零偏按均值估计）；精密补偿需扩展未知量
- **工作台碰撞**：按"末端 Z 不得低于工作台面"的平面模型校验，未做连杆级
  swept-volume 碰撞检测
- **圆弧插补**：以三点二次贝塞尔逼近弧段，大跨度圆弧需加密关键点
- 移动小程序与云端 SaaS 后台不属 Qt 技术栈，需独立立项

---

## 十一、在线演示（Vercel 静态托管）

演示页 `demo/monitor-demo.html`（零依赖单文件 HTML）已部署到 Vercel 静态托管：

- **线上地址**：`https://lingxi-polish.vercel.app/`
- **部署方式**：GitHub 导入模式（仓库 `Ziv-ZHI/lingxi-polish`），Root Directory = `demo`，
  Framework = Other，Build Command 留空；推送到 `main` 自动重部署。
- **首页约定**：Vercel 静态托管默认只认 `index.html` 当首页，故 `demo/index.html` 为
  `monitor-demo.html` 的副本；`demo/vercel.json` 仅保留 `{"version": 2}`（不依赖 rewrite 兜底）。
- **本地预览（等价校验）**：`python -m http.server` 起本地服务后访问 `demo/index.html`，
  内容与线上一致。`demo/_verify.py` / `demo/_smoke.js` 为演示页回归测试脚本（详见 `demo/VERCEL_DEPLOY.md`）。
