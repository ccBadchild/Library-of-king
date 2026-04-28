# RemoteControl（Qt + C++）

按需求实现的单程序远程桌面基础版，运行时可选择：

- 服务端模式：生成验证码，监听客户端连接并推送桌面
- 客户端模式：输入 IP、端口、验证码后连接并观看远程桌面

## 当前功能

- 单可执行文件，模式切换（服务端 / 客户端）
- 中文界面，布局风格接近 TeamViewer（左侧控制、右侧画面）
- 服务端验证码机制（6 位数字）
- 客户端可选清晰度（低清 / 标清 / 高清）
- 桌面抓取在线程中执行
- TCP 收发在线程中执行
- 脏数据传输策略（按 64x64 网格对比，仅发送变化块；变化过大时整帧发送）

## 技术实现要点

- Qt5 Widgets 做 UI
- Qt Network（`QTcpServer` / `QTcpSocket`）做通信
- 抓屏使用 `QScreen::grabWindow(0)`
- 根据清晰度预设调整分辨率和 JPEG 压缩质量
- 自定义消息协议：`Hello`、`HelloAck`、`FullFrame`、`PatchFrame`

## 环境要求

- Windows 10/11
- CMake 3.16+
- Visual Studio 2022
- Qt 5.15（默认路径：`D:/Qt/5.15/msvc2019_64`）

> 若你的 Qt 安装目录不同，配置时传入 `-DQt5_DIR=.../lib/cmake/Qt5`。

## 构建

```powershell
cmake -S . -B build -DQt5_DIR="D:/Qt/5.15/msvc2019_64/lib/cmake/Qt5"
cmake --build build --config Release
```

## 运行

```powershell
.\build\Release\RemoteControl.exe
```

程序内选择模式即可运行。
