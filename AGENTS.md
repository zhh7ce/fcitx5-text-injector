# AGENTS.md — fcitx5-text-injector

本项目是一个 fcitx5 全局模块（addon），通过 Unix socket IPC 接收外部命令，将文字提交到当前焦点应用的输入框。

## 来源

本项目从 [VocoType-linux](https://github.com/RCBosss/VocoType-linux) 的 fcitx5 集成代码中拆分而来。VocoType 是一个语音输入法项目，其 fcitx5 Module 实现了完整的"拦截按键 → 录音 → ASR → 提交文字"流程。本项目去除了语音识别部分，保留了最小化的文字注入骨架。

**使用场景**：当 `wtype`（Wayland）和 `ydotool`（X11/Wayland）在输入中文时存在兼容性问题（它们模拟按键事件，可能会和输入法框架冲突）时，本模块通过 fcitx5 的 `commitString()` API 直接提交文本，是可靠的替代方案。

## 技术栈

- **语言**：C++20
- **构建**：CMake >= 3.10
- **依赖**：Fcitx5Core（fcitx5 开发库）、nlohmann-json
- **打包**：Arch Linux PKGBUILD（makepkg）

## 架构

```
┌─────────────────────────────────────────────────┐
│  fcitx5 进程                                    │
│                                                 │
│  text_injector.so (AddonInstance)               │
│  ├── TextInjectorModule                         │
│  │   ├── EventDispatcher (线程调度)              │
│  │   ├── Key event watcher (F12 触发)           │
│  │   ├── commitText() → ic->commitString()      │
│  │   ├── showStatus() → InputPanel.setAuxUp()   │
│  │   ├── showPreedit() → InputPanel.setClientPreedit() │
│  │   └── clearUI() → InputPanel.reset()         │
│  │                                              │
│  └── IPCServer (后台线程)                        │
│      ├── Unix socket 监听                       │
│      ├── JSON 命令解析                          │
│      └── 通过 EventDispatcher 调度回调到主线程   │
└─────────────────────────────────────────────────┘
         ▲
         │ Unix socket (JSON)
         │
    外部程序 (shell/python/任意语言)
```

## 关键设计约束

### 线程安全

fcitx5 的 API（`commitString()`、`updatePreedit()`、`updateUserInterface()` 等）**只能在主事件循环线程调用**。IPC 服务端运行在独立的 accept 线程中，所有回调必须通过 `event_dispatcher_.schedule()` 调度回主线程。

```cpp
// 正确：通过 EventDispatcher 调度
ipc_server_ = std::make_unique<IPCServer>(
    socket_path_,
    [this](const std::string &text) {
        event_dispatcher_.schedule([this, text] { commitText(text); });
    },
    ...);

// 错误：直接在 IPC 线程调用 fcitx5 API
ipc_server_ = std::make_unique<IPCServer>(
    socket_path_,
    [this](const std::string &text) { commitText(text); },  // 线程不安全！
    ...);
```

### IPC 协议约定

- 每个连接处理一个请求：客户端发送 JSON → `shutdown(SHUT_WR)` → 服务端返回 JSON → 连接关闭
- 服务端读取到 EOF 后处理请求并发送响应
- 超时：接收和发送均为 2 秒
- 未知命令类型返回 `{"success": false, "error": "unknown command: ..."}`

### InputContext 获取

模块通过 `instance_->lastFocusedInputContext()` 获取当前焦点输入上下文。这意味着：
- 必须有应用处于聚焦状态且有输入上下文
- 如果焦点切换，`lastFocusedInputContext()` 会自动更新
- 不需要手动跟踪 InputContext 的生命周期

## 文件说明

| 文件 | 职责 | 行数 |
|------|------|------|
| `src/module.h` | 模块类声明、配置定义 | ~50 |
| `src/module.cpp` | 模块实现：初始化、事件处理、文字提交、UI、工厂注册 | ~200 |
| `src/ipc_server.h` | IPC 服务端接口、回调类型定义 | ~40 |
| `src/ipc_server.cpp` | Socket 监听、连接处理、JSON 解析、命令分发 | ~180 |
| `data/text_injector.conf` | fcitx5 addon 元数据 | ~10 |
| `CMakeLists.txt` | 构建配置 | ~30 |
| `packaging/arch/PKGBUILD` | Arch Linux 打包 | ~25 |

## 扩展方向

### 添加新的 IPC 命令

1. 在 `src/ipc_server.cpp` 的 `handleRequest()` 函数中添加新的 `if (type == "...")` 分支
2. 如果需要新的回调类型，在 `IPCServer` 类中添加对应的 `Handler` typedef 和成员
3. 在 `TextInjectorModule` 构造函数中传入新的回调 lambda（记得用 `event_dispatcher_.schedule()` 包装）
4. 在 `TextInjectorModule` 中实现对应的方法

### 添加 surround text 支持

参考 VocoType 的 `surroundingText()` / `deleteSurroundingText()` 用法：

```cpp
auto *ic = instance_->lastFocusedInputContext();
const auto &surrounding = ic->surroundingText();
// surrounding.text() 获取上下文文本
// surrounding.cursor() 获取光标位置
ic->deleteSurroundingText(offset, length);
```

### 添加候选词列表

```cpp
#include <fcitx/candidatelist.h>

auto &panel = ic->inputPanel();
// 创建自定义 CandidateList 并设置到 panel
```

## 构建与测试

```bash
# 构建
cd fcitx5-text-injector
mkdir build && cd build
cmake .. -DCMAKE_INSTALL_PREFIX=/usr
make -j$(nproc)

# Arch 打包
cd packaging/arch
makepkg -f

# 测试 IPC（模块需要先安装并重启 fcitx5）
echo '{"type":"ping"}' | socat - UNIX-CONNECT:$XDG_RUNTIME_DIR/text-injector.sock
echo '{"type":"commit","text":"测试"}' | socat - UNIX-CONNECT:$XDG_RUNTIME_DIR/text-injector.sock
```

## 调试

- fcitx5 日志：`journalctl --user -u fcitx5 -f` 或直接在终端运行 `fcitx5 -d --verbose`
- 检查模块是否加载：`fcitx5-diagnose`
- 检查 socket：`ls -la $XDG_RUNTIME_DIR/text-injector.sock`
- 手动测试 IPC：使用 `socat` 发送 JSON 命令
