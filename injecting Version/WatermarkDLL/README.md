# WatermarkDLL

Minecraft全版本兼容的叠加图像管理DLL。

## 修复内容

### 问题1：Fabric 1.21.4 注入后主界面点击失效
- **原因**：Fabric 的类由 KnotClassLoader 加载，JNI `FindClass` 找不到 Minecraft 类 →
  检测降级到光标兼容模式 → 主菜单光标常亮被误判为"GUI 打开"→ 编辑模式常开 →
  旧 WndProc 钩子吞掉所有鼠标消息
- **修复**：
  1. `JavaDetector` 新增 `findClassEx()`：FindClass 失败后改用
     `Thread.currentThread().getContextClassLoader().loadClass()` 再解析
     （Fabric / Forge / NeoForge 通吃），JNI 模式在 Fabric 下恢复正常
  2. 兼容模式加 `cursorWasHidden` 门控：光标先隐藏过（真正进过游戏）再变可见
     才进入编辑模式，纯主菜单不再误入
  3. WndProc 鼠标消息（按下/抬起/移动/滚轮）只在命中水印、菜单或拖拽时拦截，
     未命中一律放行给游戏

### 问题2：Fabric 1.21.4 注入后 OpenGL 被破坏
- **原因**：MC 1.17+ 使用 GL 3.2 Core Profile，旧的固定管线渲染
  （glBegin / glOrtho / glPushAttrib / wglUseFontBitmaps）全部非法，
  一调用就破坏游戏 GL 状态
- **修复**：渲染器整体重写为 Core Profile 管线
  - shader + VBO 绘制（GLSL 150 core / 120 双版本，按 GL 版本自动选择）
  - shader 时代入口（glCreateShader 等 27 个）运行时经
    `wglGetProcAddress` + `GetProcAddress(opengl32)` 双通道解析
  - 文字渲染改用 GDI 光栅化到 DIB 再上传纹理（LabelTex 缓存），
    不再依赖固定管线位图字体
  - 渲染前逐项保存 / 渲染后逐项恢复 GL 状态
    （depth / blend / cull / depthMask / program / texture / activeTexture / VAO）

### 问题3：拖拽图片到窗口添加叠加层失效
- **原因1（非 ASCII 路径）**：`std::string(filePath.begin(), filePath.end())` 把宽字符
  直接截断成 char，中文路径（用户名/文件夹名）全部变成乱码 → WIC 打不开文件 →
  拖进来的图片静默加载失败
- **修复**：新增 `WideToUtf8()`，宽字符路径统一经 `WideCharToMultiByte(CP_UTF8)`
  转换后再交给 WIC
- **原因2（提权场景 UIPI）**：游戏以管理员运行时，Windows UIPI 会静默丢弃
  Explorer 拖放发来的 `WM_DROPFILES`
- **修复**：初始化时对游戏窗口调用 `ChangeWindowMessageFilterEx` 放行
  `WM_DROPFILES` / `WM_COPYDATA` / `WM_COPYGLOBALDATA`
- **附带**：拖放过滤器补充 `.bmp`（WIC 本就支持）；WM_DROPFILES 增加调试日志，
  便于用 DebugView 诊断；启动器 DLL 释放遇文件占用时给出"请先关闭游戏"提示并
  回退到目录旁的 DLL

### 问题4：两个注入器可同时注入同一个 Minecraft
- **原因**：已注入 PID 列表只存在各自注入器进程的内存里，两个启动器实例互不知晓；
  启动器重启后对已注入的 MC 也会重复注入
- **修复（三层防护）**：
  1. DLL 加载时在目标进程创建命名互斥锁 `Local\WatermarkInjection_Active_<PID>`，
     已存在则直接拒绝二次加载（对任何注入器都生效）
  2. 启动器注入前检查该互斥锁，已注入则跳过并提示"可能另一个注入器已注入"；
     MC 列表也会过滤已注入实例
  3. 启动器单实例互斥锁：第二个启动器进程激活已有窗口后直接退出

### 问题5：聊天栏打开时拖入图片后不能直接编辑
- **原因**：从资源管理器拖文件到游戏窗口期间，游戏窗口**全程失焦**（往往持续
  数秒，远超任何去抖窗口）。光标兼容模式下旧逻辑失焦即重置 `cursorWasHidden`
  闩锁；拖放完成后聊天栏虽还开着、光标也可见，但进入编辑模式的前提条件已被
  清零 —— 必须关闭再打开一次聊天栏才能恢复编辑
- **修复**：闩锁**永不随失焦重置**；失焦只 gate 当前的 screenOpen 判定——
  Alt-Tab 切走期间编辑模式关闭，切回窗口自动恢复，闩锁保留。保留约 1 秒
  （60 帧）的失焦去抖吸收单帧抖动；拖入成功后立即初始化新叠加层几何并选中它
  （显示选中边框，即拖即用）

### 问题6：随机移动模式只能上下移动
- **原因**：移动步长用 `(int)(方向分量 × 2)` 取整——`|cos(角度)| < 0.5` 时
  水平分量每帧取整为 0，而方向只在撞墙时才重新随机，导致约三分之一概率
  选到水平分量为 0 的方向，叠加层终生只能上下滑
- **修复**：引入亚像素累积器 + 按 50ms（mod 的 client tick 间隔）计步，
  两个轴按方向向量等比例移动，总速度 2px/50ms 与 mod 一致；撞墙时清零累积器

### 问题7：按下 F1 图片闪烁
- **原因**：用 `GetAsyncKeyState(VK_F1)` 检测 —— 它反映的是**物理按键状态**，
  而 MC 的隐藏 HUD 是**切换式**的。快速点按 F1 时按键只落下几帧：图片闪一下就
  回来，但 HUD 其实一直藏着，状态完全对不上
- **修复**：优先经 JNI 直接读取 `options.hudHidden`（Yarn / Mojmap / MCP 1.8.9
  多映射名自动解析）；JNI 不可用时回退为自己在 WndProc 里跟踪 F1 切换状态
  （聊天栏等屏幕打开时与原版一致不切换）

### 问题8：改变窗口大小后贴图尺寸不变
- **原因**：贴图显示尺寸 = 原图像素 × 固定倍率，不随窗口变化；位置虽是分数坐标
  会跟着走，但大小在窗口变大后显得越来越小
- **修复**：注入时记录基准窗口宽度，每帧计算比例系数
  `当前宽度 / 基准宽度`，显示尺寸 = 原图 × scale × 系数 —— 所有贴图（含后来
  拖入的）随窗口等比缩放；存档的 scale 不含系数，窗口恢复原尺寸时贴图严格复原。
  角拖拽改尺寸时会除掉系数再存 scale，避免缩放叠加

### 问题9：贴图在主界面显示
- **原因**：原实现只要不在 F1 隐藏状态就渲染，注入后主界面/服务器列表
  也能看到贴图
- **修复**：贴图只在**世界加载后**渲染。JNI 可用时直接读
  `minecraft.world/level` 字段（Yarn / Mojmap / MCP 1.8.9 / Fabric
  intermediary 多映射名解析，主界面、服务器列表、加载画面均为 null）；
  兼容模式用"光标曾在游戏内隐藏过"的闩锁近似（局限：退回主界面后闩锁仍保持，
  该场景下贴图会显示到游戏重启）

### 问题10：GIF 动图播放时位移、背景乱码
- **原因**：GIF 帧是带偏移（`/imgdesc/Left|Top`）的局部更新，且每帧声明
  处置方式（disposal 1 保留 / 2 清为透明 / 3 恢复上一帧快照）。旧代码把每帧
  当全图、直接从 malloc 缓冲上传——未初始化背景像素和上一帧残留被当成画面
  内容，合成到 GL 纹理后即表现为位移与乱码
- **修复**：重写 WIC GIF 解码为**整图画布合成**：零初始化透明画布 → 按上一帧
  disposal 处理（保留 / 清零矩形 / 恢复快照）→ 按帧偏移做 straight-alpha
  合成 → 每帧输出整图画布副本逐帧上传；加载侧加空帧保护
- **验证**：用 `%APPDATA%/WatermarkDLL/images/` 下 4 个真实水印 GIF 与 PIL
  逐帧交叉比对，所有双方都不透明像素 diff = 0（差异仅在透明区，视觉无关）

### 问题11：Fabric 下进入世界再退回主菜单，贴图仍显示
- **原因**：Fabric 运行时使用 intermediary 混淆名，检测表此前只有 Yarn /
  Mojmap / MCP 命名 → JNI 全部失效 → 降级兼容模式 → 只能用"光标曾隐藏过"
  的闩锁近似世界内状态，退回主菜单后闩锁不清零，贴图继续显示
- **修复**：从本机 Fabric loom 缓存解析 1.21.4 权威映射，将 intermediary
  类/字段名加入检测表（`class_310` / `field_1700` / `field_1755` /
  `field_1687` / `class_437` / `class_408` / `class_315` / `field_1690` /
  `field_1842` / `class_638`），JNI 模式在 Fabric 下恢复——世界状态直读、
  退回主菜单立即隐藏贴图。intermediary 名跨版本稳定，适配新版本无需再补映射

### 问题12：白色方块修复
- **原因**：创建了自定义GL上下文，与Minecraft的上下文不共享纹理
- **修复**：直接使用Minecraft当前的GL上下文进行渲染

### 变更：贴图始终显示（按需求移除隐藏功能）
- **移除 1（主菜单不显示）**：删除"仅世界加载后渲染"的门控
  （`g_inGame` / JNI world 检测 / 兼容闩锁），贴图在**主菜单、服务器列表、
  游戏内始终显示**
- **移除 2（显隐切换）**：删除按键绑定（右键菜单项、按键捕获、按绑定键
  显隐）、双击切换显隐、`visible` / `boundKeyCode` 字段及配置项、
  F1 隐藏 HUD 时的渲染跳过（`hudHidden` 门控与 F1 状态跟踪）——
  贴图**永远全亮显示**，按 F1 不再影响贴图
- **影响**：旧 `overlays.json` 中多余的 `visible` / `boundKeyCode` 字段
  加载时自动忽略，无需手动清理

### 问题13：GIF支持
- **原因**：原代码不支持GIF动画
- **修复**：基于 WIC 实现图像加载模块，支持PNG/JPG/BMP/GIF格式
- **GIF动画**：完整多帧支持，按帧延迟播放，驱动路径点/随机展示切换节奏

### 问题14：聊天屏幕检测（全版本兼容）
- **原因**：使用Insert键触发编辑模式，不是原生方案
- **修复**：实现JNI检测模块，通过反射读取Minecraft内部状态
- **兼容性**：支持Minecraft 1.8.9+所有版本，自动查找类和字段

## 功能

- 支持PNG/JPG/BMP/GIF(多帧动画)格式图像叠加
- 拖放图像文件到游戏窗口添加叠加层
- 四种移动模式：静止、定点移动(路径点编辑)、随机移动、随机位置
- 右键菜单（中文，5项）：定点移动 / 随机运动 / 随机展示 / 重置运动 / 删除
- 路径点拾取：左键添加标点(最多7个)、右键撤销、Enter确认、Esc取消
- 聊天栏打开时自动进入编辑模式（自动检测），Insert 键手动切换
- 贴图始终显示：主菜单/游戏内均渲染，按 F1 不影响，无显隐切换
- 配置自动保存到 `%APPDATA%/WatermarkDLL`（含移动模式/路径点/随机方向），重启自动恢复
- 保存节流：1秒内多次修改只落盘一次，移动循环中自动补写

## 使用方法

### 注入DLL

使用DLL注入器将 `WatermarkDLL.dll` 注入到Minecraft进程中。

### 操作说明

| 操作 | 说明 |
|------|------|
| 拖放文件 | 添加PNG/JPG/BMP图像为叠加层 |
| 打开聊天栏 | 进入编辑模式（自动检测） |
| 关闭聊天栏 | 退出编辑模式 |
| 左键拖动 | 移动叠加层（编辑模式） |
| 右键菜单 | 更多选项（编辑模式） |
| 滚轮 | 缩放叠加层（编辑模式） |

## 技术实现

### 聊天屏幕检测（全版本兼容）

双模式自动切换：

1. **JNI 模式**（优先）：反射读取 Minecraft 内部状态（1.8.9 / 1.12.2 / Fabric 等已知映射）
2. **光标兼容模式**（自动降级）：参考 InfiniteGUI-DLL 思路——纯 Win32 检测光标可见性，
   游戏内光标隐藏、任何 GUI 界面（聊天/背包/暂停）显示系统光标，**不依赖任何混淆类名，
   适配所有 Minecraft 版本**。仅游戏窗口聚焦时判定。

### GUI 缩放适配（全版本一致）

- 从 `.minecraft/options.txt` 读取 `guiScale`，按 MC 官方算法计算缩放系数（所有版本相同）
- 叠加层坐标使用 MC 缩放后坐标系，与 mod 版位置完全一致，跨版本/分辨率/GUI 缩放不再漂移
- 鼠标输入按缩放系数自动换算

### 图像加载

使用Windows原生API加载图像：
- 支持PNG、JPG、BMP格式
- 自动转换为RGBA格式
- 创建OpenGL纹理

## 文件结构

```
WatermarkDLL/
├── WatermarkDLL.cpp          # 主模块
├── WatermarkDLL.vcxproj      # 项目文件
├── stb_image_impl.cpp        # 图像加载实现
├── JavaDetector.h/cpp        # JNI检测模块
├── detours/                  # Hook库
└── x64/Release/
    └── WatermarkDLL.dll      # 编译生成的DLL
```

## 编译

使用Visual Studio 2022打开 `WatermarkDLL.sln`，选择Release x64编译。

## 配置文件位置

- 配置目录: `%APPDATA%/WatermarkDLL`
- 图像备份: `%APPDATA%/WatermarkDLL/images/`
- 配置文件: `%APPDATA%/WatermarkDLL/overlays.json`

## 已知限制

- 需要Java环境（Minecraft自带）
- 某些自定义客户端可能需要额外的类名映射

## 后续优化

- 完整GIF多帧动画支持
- 更多图像格式支持
- 配置界面优化
- 性能优化
