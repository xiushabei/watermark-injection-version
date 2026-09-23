# Watermark 注入版（watermark-injection-version）

Minecraft 全版本兼容的**水印贴图注入方案**：一个 ImGui 注入启动器 + 一个内嵌的
Watermark DLL。启动器把 DLL 释放到 `%APPDATA%/WatermarkInjection/` 并注入
Minecraft 进程，DLL 在游戏画面（主菜单 / 游戏内 / 任何界面）上以 OpenGL 叠加
PNG / JPG / BMP / GIF（含多帧动画）贴图，支持拖放添加、定点移动、随机运动、
随机展示等多种玩法。贴图**始终显示**，无显隐切换。

> 本项目由原版 Fabric mod（`watermark-2.0.jar`）移植而来：
> 不依赖任何 mod 加载器，注入即用于几乎任何 Minecraft 版本（1.8.9+，
> Fabric / Forge / NeoForge / 原版均适用）。

## 仓库结构

```
├── Launcher/                  # 注入启动器（ImGui 界面，内嵌 DLL）
│   ├── launcher.cpp           #   启动器主程序（托盘/自动注入/进程列表/日志）
│   ├── launcher.rc            #   资源脚本：把 WatermarkInjection.dll 编译为内嵌资源
│   ├── build_launcher.ps1     #   一键编译脚本（VS 2022 BuildTools cl.exe）
│   ├── imgui*.cpp/h           #   Dear ImGui + Win32/OpenGL3 后端
│   └── WatermarkInjection.dll #   内嵌用 DLL（由 WatermarkDLL 编译产物复制而来）
│
├── injecting Version/
│   └── WatermarkDLL/          # 水印 DLL 本体（Visual Studio 工程）
│       ├── WatermarkDLL.cpp   #   主模块：渲染器 / WndProc 钩子 / 叠加层管理
│       ├── JavaDetector.h/cpp #   JNI 屏幕检测（Yarn/Mojmap/MCP/Fabric intermediary）
│       ├── stb_image_impl.cpp #   图像加载（WIC，GIF 整图画布合成解码）
│       ├── detours/           #   Microsoft Detours + lazy_importer（含预编译 detours.lib）
│       ├── WatermarkDLL.sln   #   VS 2022 解决方案
│       └── README.md          #   DLL 详细修复记录与技术文档
│
└── _analysis/                 # 开发期分析/验证脚本（GIF 逐帧比对工具等）
```

## 快速开始（使用者）

1. 编译或用 Release 拿到 `WatermarkInjectionLauncher.exe`（内嵌最新 DLL）；
2. 运行启动器，它会常驻系统托盘并**自动扫描 / 注入已启动的 Minecraft**；
3. 把图片文件**直接拖进游戏窗口**即添加贴图；按编辑模式键（默认 Insert）
   进入编辑模式后拖动移动、滚轮缩放、双击切换贴图隐藏、右键菜单选择运动模式。

配置文件与贴图备份存放在 `%APPDATA%/WatermarkDLL/`（`overlays.json` + `images/`），
重装 / 换机时拷贝这个目录即可恢复全部贴图与位置。

## 编译

### 环境要求

- Windows 10/11 x64
- Visual Studio 2022（含 C++ 桌面开发与 Windows 10/11 SDK）
- JDK 17（仅编译 WatermarkDLL 时需要 JNI 头文件；游戏本身自带 JRE 运行不需要）

### 1. 编译 WatermarkDLL

用 Visual Studio 2022 打开 `injecting Version/WatermarkDLL/WatermarkDLL.sln`，
选择 **Release / x64** 生成，产物在 `x64/Release/WatermarkDLL.dll`。

### 2. 更新启动器内嵌的 DLL

把上一步的 `WatermarkDLL.dll` 复制为 `Launcher/WatermarkInjection.dll`：

```powershell
copy "injecting Version\WatermarkDLL\x64\Release\WatermarkDLL.dll" "Launcher\WatermarkInjection.dll"
```

### 3. 编译启动器

```powershell
powershell -ExecutionPolicy Bypass -File Launcher\build_launcher.ps1
```

产物为 `Launcher/WatermarkInjectionLauncher.exe`（单文件，无需附带 DLL）。

## 启动器功能

- **内嵌 DLL**：启动器把 DLL 作为资源编译进 exe，运行时释放到
  `%APPDATA%/WatermarkInjection/WatermarkInjection.dll` 再注入
  （释放失败，例如游戏正在运行占用文件时，自动回退使用启动器目录旁的 DLL）
- **自动注入**：监视新启动的 Minecraft 进程，自动完成注入
- **防重复注入**：目标进程内命名互斥锁 + 启动器注入前检查 + 启动器单实例互斥锁，
  三个实例同时开也只会有一个生效
- **托盘常驻**：关闭窗口最小化到托盘，可一键显示 / 注入 / 退出
- **进程列表**：手动选择任意 Minecraft 进程注入
- **编辑模式按键设置**：界面一键改键（默认 Insert），写入
  `%APPDATA%/WatermarkDLL/edit_key.cfg`，DLL 每 500ms 轮询，已注入的游戏
  进程内即时生效
- **运行日志**：界面内实时显示注入状态与错误

## 核心机制（DLL）

- **渲染**：OpenGL Core Profile 管线（shader + VBO），渲染前逐项保存、渲染后
  逐项恢复游戏 GL 状态，不污染 Minecraft 的上下文
- **编辑模式**：纯按键切换（默认 Insert，启动器可改），不依赖聊天栏 / GUI
  屏幕检测；编辑模式下双击贴图切换隐藏状态，隐藏贴图平时不渲染、
  编辑模式下淡显以便找回
- **GUI 缩放**：读取 `.minecraft/options.txt` 的 `guiScale`，贴图坐标与原版
  mod 完全一致，跨分辨率 / GUI 缩放不漂移
- **窗口等比缩放**：记录基准窗口宽度，窗口改变大小时所有贴图等比缩放，
  复原时严格还原

详细修复记录（Fabric 点击失效、OpenGL 破坏、GIF 位移乱码等十余项问题）见
[`injecting Version/WatermarkDLL/README.md`](injecting Version/WatermarkDLL/README.md)。

## 操作说明

| 操作 | 说明 |
|------|------|
| 拖放文件到游戏窗口 | 添加 PNG/JPG/BMP/GIF 贴图 |
| 编辑模式键（默认 Insert，启动器可改） | 进入 / 退出编辑模式 |
| 双击贴图 | 切换该贴图隐藏/显示（编辑模式） |
| 左键拖动 | 移动贴图（编辑模式） |
| 右键 | 打开菜单：定点移动 / 随机运动 / 随机展示 / 重置运动 / 删除 |
| 滚轮 | 缩放贴图（编辑模式） |
| 拖角点 | 等比调整贴图尺寸 |

## 已知限制

- 需要 Java 环境（Minecraft 自带）
- 某些自定义客户端可能需要补充类名映射（见 DLL README）
