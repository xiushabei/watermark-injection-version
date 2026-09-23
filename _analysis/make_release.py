# -*- coding: utf-8 -*-
"""Create the GitHub v1.2.0 release and upload the launcher exe."""
import json
import subprocess
import sys

import requests

REPO = "xiushabei/watermark-injection-version"
TAG = "v1.2.0"
EXE = r"D:\watermark injecting Version\Launcher\WatermarkInjectionLauncher.exe"

# Token from the git credential manager (same one git push uses)
out = subprocess.run(
    ["git", "credential", "fill"],
    input="protocol=https\nhost=github.com\n\n",
    capture_output=True, text=True, cwd=r"D:\watermark injecting Version",
).stdout
token = next(l.split("=", 1)[1] for l in out.splitlines() if l.startswith("password="))
headers = {
    "Authorization": f"Bearer {token}",
    "Accept": "application/vnd.github+json",
    "X-GitHub-Api-Version": "2022-11-28",
}

body = """## v1.2.0

### 新功能
- **编辑模式按键可配置**：启动器界面「编辑模式按键 → 修改」，按下新按键即绑定（Esc 取消），默认 Insert；写入 `%APPDATA%/WatermarkDLL/edit_key.cfg`，DLL 每 500ms 轮询，**已注入的游戏进程内即时生效**
- **编辑内双击显隐**：编辑模式下双击贴图切换隐藏/显示；隐藏贴图平时完全跳过渲染，编辑模式下以 30% 透明度淡显便于找回；状态存入 `overlays.json`

### 变更
- 编辑模式不再依赖聊天栏自动检测：JNI 屏幕检测、光标兼容检测、光标闩锁、失焦去抖整套机制退役（代码保留备查），编辑模式**仅由按键切换**

### 使用
1. 运行 `WatermarkInjectionLauncher.exe`（单文件，内嵌最新 DLL，自动扫描/注入 Minecraft）
2. 把图片拖进游戏窗口添加贴图
3. 按编辑模式键（默认 Insert）进入编辑模式：拖动移动、滚轮缩放、双击显隐、右键菜单选择运动模式

配置文件与贴图备份：`%APPDATA%/WatermarkDLL/`（`overlays.json` + `images/`）
"""

r = requests.post(
    f"https://api.github.com/repos/{REPO}/releases",
    headers=headers,
    json={"tag_name": TAG, "name": TAG + " — 编辑模式按键可配置",
          "body": body, "draft": False, "prerelease": False},
    timeout=60,
)
print("create release:", r.status_code)
if r.status_code != 201:
    print(r.text[:500]); sys.exit(1)
rel = r.json()
upload_url = rel["upload_url"].split("{")[0]

with open(EXE, "rb") as f:
    data = f.read()
r2 = requests.post(
    upload_url,
    params={"name": "WatermarkInjectionLauncher.exe"},
    headers={**headers, "Content-Type": "application/octet-stream"},
    data=data,
    timeout=300,
)
print("upload asset:", r2.status_code)
if r2.status_code != 201:
    print(r2.text[:500]); sys.exit(1)
print("RELEASE_URL=" + rel["html_url"])
print("ASSET_SIZE=" + str(r2.json()["size"]))
