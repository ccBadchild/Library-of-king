# RemoteControl

RemoteControl 是一款面向局域网的 **Windows 远程桌面** 程序：同一套 Qt/C++ 可执行文件可在运行时切换 **服务端** 与 **客户端**，完成桌面画面推送、远端键鼠控制与连接鉴权。界面为中文暗色主题，支持无边框主窗口、系统托盘与独立的远程画面顶层窗口。

---

## 功能概览

### 运行模式与界面

- **一体化进程**：单个 `RemoteControl.exe` 内切换「服务端 / 客户端」，无需两套安装包。
- **无边框主窗口**：自定义标题栏（拖拽、最小化、最大化、关闭）；最小化后主界面隐藏至 **系统托盘**（托盘菜单可恢复窗口或退出）。
- **独立远程画面窗口**：客户端连接后，远端桌面在 **独立顶层窗口**（`ClientVideoWindow`）中显示，便于与本地控制中心并排摆放；关闭该窗口时可选择是否断开连接。
- **状态联动**：服务端已启动或客户端已连接时，界面会锁定相关控件（例如禁止重复启动服务端、禁止切换运行模式），避免误操作。

### 网络与鉴权

- **TCP 长连接**：自定义二进制协议（`protocol_qt.h`），消息格式为「类型 + 负载长度 + 负载」。
- **握手鉴权**：服务端启动后生成 **6 位数字验证码**；客户端输入服务端 IP、端口与验证码完成 `Hello` / `HelloAck` 握手后方可收发画面与控制指令。
- **Ping / Pong**：用于连接存活检测。

### 画面采集与编码（服务端）

- **屏幕捕获**：Windows **GDI**（`BitBlt` / `GetDIBits`）抓取主显示器整屏，逻辑与清晰度预设结合后得到发送分辨率。
- **JPEG**：支持 **整帧** 与 **补丁帧**（脏矩形分块），在变化较小时降低带宽。
- **H.264**：通过 **FFmpeg**（libavcodec）编码，封装为 `VideoFrame` 消息；编码器优先尝试 **NVENC / AMF / QSV** 等硬件路径，失败时回落 **libx264**。
- **自适应 QoS**：根据发送积压等动态调整 JPEG 质量与采集节奏（见 `capture_worker` 中的统计与节流逻辑）。
- **清晰度档位**：客户端可选低 / 标清 / 高清，影响缩放比例、JPEG 质量及「自动」编码下的协商策略。

### 解码与呈现（客户端）

- **H.264 解码**：FFmpeg 解码裸 H.264 码流并送显。
- **渲染路径**：可选择 **OpenGL** 渲染或 **软件渲染**（兼容无可用 GL 的环境）。

### 远程控制

- **输入回传**：鼠标移动、按下、抬起、滚轮与键盘按下/抬起等事件封装为 `InputEvent` 发往服务端。
- **只看 / 可控**：提供「只看模式」与可控模式切换（可控时向远端注入输入）。

### 监控与运维

- **QoS 显示**：服务端展示 FPS、码率、JPEG 质量等；客户端展示 FPS、码率、重连次数、延迟、分辨率等。
- **自动重连**：客户端断线后按策略重连。
- **日志**：服务端与客户端日志分别写入文件（默认在可执行文件目录下 `logs/`），界面也有运行日志区。

### 其他

- **可执行文件图标**：通过 `RemoteControl.rc` 嵌入 `icons/app.ico`，供资源管理器显示。

---

## 开发与构建环境

| 项目 | 说明 |
|------|------|
| 操作系统 | **仅支持 Windows**（工程在 `CMakeLists.txt` 中非 Win32 会直接失败） |
| 编译器 | **MSVC**（示例：Visual Studio 2022） |
| CMake | **3.16+** |
| C++ 标准 | **C++17** |
| Qt | **5.15**（Widgets、Network、Gui、Core）；默认 CMake 缓存路径示例见下表 |
| 资源编译 | CMake 启用 **RC**，用于嵌入图标 |

默认 Qt 路径（可通过 `-DQt5_DIR=...` 覆盖）：

`D:/Qt/5.15/5.15.2/msvc2019_64/lib/cmake/Qt5`

---

## 第三方依赖

| 依赖 | 用途 | 说明 |
|------|------|------|
| **Qt 5** | GUI、网络、图像与跨线程信号槽等 | 以 **动态库** 形式链接（`Qt5Core.dll` 等），需遵守 [Qt 许可](https://doc.qt.io/qt-5/licensing.html)（开源版通常为 **LGPL v3**，亦提供商业授权） |
| **FFmpeg**（libavcodec、libavutil、libswscale 等） | H.264 编码/解码、色彩空间转换 | 工程假定开发包位于仓库根目录 **`ffmpeg-8.1-full_build-shared/`**（与 `CMakeLists.txt` 中 `FFMPEG_ROOT` 一致）；该目录在 **`.gitignore` 中忽略**，需自行下载匹配位数的 **shared 构建**，将 `include/`、`lib/`、`bin/` 放到该路径；构建后脚本会把运行时所需 **DLL** 复制到输出目录 |
| **Windows API** | 抓屏（GDI）、输入注入（`SendInput`）等 | 随系统提供 |

**分发说明**：除本仓库源码与自构建产物外，请向终端用户提供 **Qt 运行时 DLL**、**FFmpeg DLL**（及若使用到的 **VC 运行库**）。FFmpeg 二进制请同时保留上游随包提供的 **版权声明**（如 `COPYING.LGPLv2.1`、`LICENSE` 等），以符合其许可证要求。

---

## 开源许可

- **本仓库中 RemoteControl 的原创源代码**（不含 Qt / FFmpeg 等第三方二进制与头文件本体）在默认理解下适用于 **GNU 通用公共许可证第 3 版（GNU GPLv3）**，完整条文见仓库根目录 **[LICENSE](LICENSE)**。
- **Qt**：若使用 Qt 开源版本，通常以 **LGPL v3**（或你选择并采购的 **商业许可**）为准，请参阅安装目录与 [官方许可说明](https://doc.qt.io/qt-5/licensing.html)。
- **FFmpeg**：官方以 **LGPL / GPL** 等组合授权，具体取决于你选用的预编译组件与编译选项；请以所使用版本随附的 **`LICENSE` / `COPYING*` 文件为准**。

若你不希望采用 GPLv3 约束自己的下游分发方式，可自行评估是否满足 **Qt LGPL** 的动态链接与再链接等义务，并结合 **FFmpeg** 的实际授权选择（包括是否仅使用 LGPL 组件、是否提供源代码等），必要时咨询法律顾问。

---

## 获取 FFmpeg 开发包（未随仓库提供）

将官方或可信渠道下载的 **shared** 前缀构建解压/重命名为：

`ffmpeg-8.1-full_build-shared/`

并与 `CMakeLists.txt` 同级，目录中应包含 `include`、`lib`、`bin`。

---

## 构建步骤

```powershell
cmake -S . -B build -DQt5_DIR="D:/Qt/5.15/5.15.2/msvc2019_64/lib/cmake/Qt5"
cmake --build build --config Release
```

若曾大幅修改 CMake 或资源脚本，必要时先 `cmake --fresh` 再配置，以确保资源编译生效。

Release 输出示例路径：

`build\Release\RemoteControl.exe`

---

## 运行

```powershell
.\build\Release\RemoteControl.exe
```

在界面中选择 **服务端** 或 **客户端**，按提示配置端口、验证码或连接参数即可。

---

## 日志位置（默认）

- 服务端：`build/Release/logs/server.log`
- 客户端：`build/Release/logs/client.log`

（实际路径以运行时工作目录为准。）
