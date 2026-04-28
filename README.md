# RemoteControl (Qt + C++)

RemoteControl 是一个基于 Qt/C++ 的远程桌面软件，采用“服务端 / 客户端”一体化设计。  
同一个程序内可切换运行模式，支持局域网远程桌面查看与控制、连接鉴权、质量调节与实时状态监控。

## 主要功能

- **一体化模式切换**
  - 同一可执行程序支持服务端模式与客户端模式。
  - 服务端模式下展示连接信息与日志；客户端模式下展示远程桌面与控制能力。

- **连接鉴权**
  - 服务端启动后自动生成 6 位连接验证码。
  - 客户端需输入 IP、端口、验证码完成握手验证。

- **远程桌面传输**
  - 服务端持续抓取桌面画面并通过 TCP 推送。
  - 支持整帧与补丁帧（脏块）两种传输方式，减少带宽占用。
  - 客户端支持 OpenGL 渲染与软件渲染双模式切换。

- **清晰度与自适应**
  - 客户端可选择低清/标清/高清。
  - 服务端根据网络发送积压动态调整 JPEG 质量与采集节奏。

- **输入回传**
  - 支持鼠标移动、按下、抬起、滚轮以及键盘按键回传。
  - 提供“只看模式 / 可控模式”切换。

- **QoS 可视化**
  - 服务端显示 FPS、码率、JPEG 质量。
  - 客户端显示 FPS、码率、重连次数、延迟、分辨率。

- **自动重连与日志**
  - 客户端断线自动重连。
  - 服务端与客户端日志分开写入，便于排障。

## 技术实现概要

- UI: Qt5 Widgets（中文界面，暗色风格）
- 网络: `QTcpServer` / `QTcpSocket`
- 抓屏: Windows GDI 抓屏（线程执行）
- 编码: JPEG 压缩
- 渲染: `QOpenGLWidget` + 软件渲染备用
- 输入注入: Windows `SendInput`
- 协议: 自定义消息帧（Hello/HelloAck/FullFrame/PatchFrame/InputEvent/Ping/Pong）

## 运行环境

- Windows 10/11
- CMake 3.16+
- Visual Studio 2022 (MSVC)
- Qt 5.15（默认示例：`D:/Qt/5.15/5.15.2/msvc2019_64`）

## 构建步骤

```powershell
cmake -S . -B build -DQt5_DIR="D:/Qt/5.15/5.15.2/msvc2019_64/lib/cmake/Qt5"
cmake --build build --config Release
```

## 启动方式

```powershell
.\build\Release\RemoteControl.exe
```

启动后在界面选择“服务端”或“客户端”模式即可使用。

## 日志位置

- 服务端日志：`build/Release/logs/server.log`
- 客户端日志：`build/Release/logs/client.log`

## 旧版说明文档

原始 README 已按要求保留并重命名为：`README.old.md`。
