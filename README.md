# fcitx5-text-injector

一个轻量级的 fcitx5 全局模块（addon），允许外部程序通过 Unix socket 向任意应用的当前焦点输入框提交文字。

## 背景

本项目从 [VocoType-linux](https://github.com/RCBosss/VocoType-linux)（语音输入法）中拆分而来。VocoType 的 fcitx5 集成部分实现了一个完整的"通过输入法框架向焦点应用注入文字"的机制。本项目提取了其中的最小骨架，去除了所有语音识别相关逻辑，保留纯粹的文字注入能力。

**解决的核心问题**：在 Linux 下，`wtype`（Wayland）和 `ydotool`（X11/Wayland）等虚拟键盘工具在输入中文时存在兼容性问题——它们模拟的是按键事件，可能会和输入法框架产生冲突，导致输入结果不符合预期。本项目通过直接调用 fcitx5 的 `commitString()` API 绕过这一问题，实现可靠的中文（及任意 Unicode 文本）注入。

## 原理

本模块作为 fcitx5 的插件加载，运行在 fcitx5 进程内部。它启动一个 Unix socket 服务端，接收外部程序发来的 JSON 命令，然后通过 fcitx5 的 `InputContext::commitString()` API 将文字注入到当前焦点应用的输入框中。

```
外部程序                          fcitx5 进程
   │                                │
   │  Unix Socket (JSON)            │  ┌──────────────────────────────┐
   ├───────────────────────────────►│  │ text_injector.so             │
   │  {"type":"commit",             │  │                              │
   │   "text":"你好世界"}            │  │  IPC Server ──► commitString()│
   │                                │  │                              │
   │  ◄── {"success":true} ────────┤  │                              │
   │                                │  └──────────────────────────────┘
```

## 功能

- **文字提交**：通过 IPC 命令将文字直接提交到当前焦点输入框
- **状态显示**：在 fcitx5 输入面板显示状态消息
- **预编辑文本**：显示预编辑（preedit）文本，模拟输入法组合状态
- **面板清除**：清除输入面板上的所有自定义内容
- **线程安全**：IPC 线程通过 EventDispatcher 调度回 fcitx5 主事件循环

## IPC 协议

### Socket 路径

默认：`$XDG_RUNTIME_DIR/text-injector.sock`

备选：`/tmp/text-injector-<uid>.sock`

可通过 fcitx5 配置工具修改，或在配置文件 `~/.config/fcitx5/conf/text_injector.conf` 中设置：

```ini
[TextInjector]
SocketPath=/custom/path/to/socket.sock
```

### 命令格式

每个请求是一个 JSON 对象，通过 Unix stream socket 发送。发送完毕后关闭写端（`SHUT_WR`），服务端读取到 EOF 后返回 JSON 响应。

#### `ping` — 健康检查

```json
# 请求
{"type": "ping"}

# 响应
{"pong": true}
```

#### `commit` — 提交文字

将文字提交到当前焦点应用的输入框。支持多行文本：`text` 中的 `\n` 会被作为换行符插入（等同于粘贴行为，不会触发 Enter/Return 键事件）。

```json
# 请求
{"type": "commit", "text": "你好世界"}

# 多行示例
{"type": "commit", "text": "第一段\n第二段"}

# 响应
{"success": true}

# 错误响应（无焦点上下文时）
{"success": false, "error": "no focused input context"}
```

#### `show` — 显示状态消息

在 fcitx5 输入面板的辅助区域显示一条消息。

```json
# 请求
{"type": "show", "text": "正在处理..."}

# 响应
{"success": true}
```

#### `preedit` — 显示预编辑文本

在输入面板显示预编辑文本，模拟输入法正在组合的状态。

```json
# 请求
{"type": "preedit", "text": "nihao", "cursor": 5}

# 响应
{"success": true}
```

#### `clear` — 清除面板

清除输入面板上由本模块设置的所有内容。

```json
# 请求
{"type": "clear"}

# 响应
{"success": true}
```

### 使用示例

#### Shell（需要 `socat`）

```bash
# 健康检查
echo '{"type":"ping"}' | socat - UNIX-CONNECT:$XDG_RUNTIME_DIR/text-injector.sock

# 向当前焦点窗口输入文字
echo '{"type":"commit","text":"Hello, 世界!"}' | socat - UNIX-CONNECT:$XDG_RUNTIME_DIR/text-injector.sock

# 显示状态
echo '{"type":"show","text":"识别中..."}' | socat - UNIX-CONNECT:$XDG_RUNTIME_DIR/text-injector.sock

# 清除面板
echo '{"type":"clear"}' | socat - UNIX-CONNECT:$XDG_RUNTIME_DIR/text-injector.sock
```

#### Python

```python
import json
import socket
import os

def send_command(cmd: dict) -> dict:
    sock_path = os.path.expandvars("$XDG_RUNTIME_DIR/text-injector.sock")
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as s:
        s.connect(sock_path)
        s.sendall(json.dumps(cmd).encode())
        s.shutdown(socket.SHUT_WR)
        data = s.recv(4096)
        return json.loads(data)

# 提交文字
send_command({"type": "commit", "text": "你好"})

# 健康检查
print(send_command({"type": "ping"}))
```

#### 配合语音识别使用

```bash
# 示例：将 whisper.cpp 的输出通过本模块输入到当前应用
text=$(./main -m model.ggml.bin -f audio.wav -l auto)
echo "{\"type\":\"commit\",\"text\":\"$text\"}" | socat - UNIX-CONNECT:$XDG_RUNTIME_DIR/text-injector.sock
```

## 构建与安装

### 依赖

- fcitx5（含开发头文件）
- nlohmann-json
- cmake >= 3.10
- C++20 编译器

### Arch Linux

```bash
cd packaging/arch
makepkg -f
sudo pacman -U fcitx5-text-injector-*.pkg.tar.zst
```

### 手动构建

```bash
mkdir build && cd build
cmake .. -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_BUILD_TYPE=Release
cmake --build build
sudo cmake --install build
```

### 安装后

1. 重启 fcitx5：`fcitx5 -r`
2. 验证模块加载：`fcitx5-diagnose` 中应看到 Text Injector 模块
3. 检查 socket 存在：`ls $XDG_RUNTIME_DIR/text-injector.sock`

### 卸载

```bash
sudo pacman -R fcitx5-text-injector
```

## 配置

通过 fcitx5 配置工具（`fcitx5-configtool`）或编辑配置文件：

`~/.config/fcitx5/conf/text_injector.conf`

```ini
[TextInjector]
# 自定义 socket 路径（留空使用默认路径）
SocketPath=
```

## 项目结构

```
fcitx5-text-injector/
├── CMakeLists.txt              # 构建配置
├── README.md                   # 本文件
├── AGENTS.md                   # AI 代理开发指南
├── data/
│   └── text_injector.conf      # fcitx5 addon 描述文件
├── src/
│   ├── module.h                # 模块头文件
│   ├── module.cpp              # 模块核心实现
│   ├── ipc_server.h            # IPC 服务端头文件
│   └── ipc_server.cpp          # IPC 服务端实现
└── packaging/
    └── arch/
        └── PKGBUILD            # Arch Linux 打包
```

## 与 wtype / ydotool 的对比

| | wtype / ydotool | fcitx5-text-injector |
|---|---|---|
| **原理** | 模拟内核级按键事件（uinput/XTEST） | 调用 fcitx5 `commitString()` API |
| **中文输入** | 存在兼容性问题，可能会和输入法冲突 | 可靠，直接提交最终文本 |
| **Wayland 支持** | wtype 仅 Wayland，ydotool 需权限 | 依赖 fcitx5，X11/Wayland 均可 |
| **权限要求** | ydotool 需要 uinput 设备权限 | 无额外权限要求 |
| **适用场景** | 输入英文、快捷键模拟 | 输入中文/日文/emoji 等任意 Unicode |
| **前置条件** | 无 | 需要 fcitx5 输入法框架运行 |

当 `wtype "你好"` 或 `ydotool type "你好"` 出现兼容性问题（如输出乱码、与输入法冲突等）时，本模块是可靠的替代方案。

## 致谢

本项目的 fcitx5 集成代码提取自 [VocoType-linux](https://github.com/RCBosss/VocoType-linux)（Apache-2.0 许可），感谢 VocoType 作者的出色工作。VocoType 是一个完整的语音输入法项目，支持离线 ASR 和 AI 润色，推荐有需要的用户使用。

## 许可证

MIT
