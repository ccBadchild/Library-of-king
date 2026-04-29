#pragma once

/**
 * @file mainwindow.h
 * @brief 远程桌面应用主界面：聚合服务端/客户端控制器、左侧控制面板、独立远程画面窗口与系统托盘入口。
 */

#include "client_controller.h"
#include "client_video_window.h"
#include "file_transfer_dialog.h"
#include "server_controller.h"
#include "tcp_file_client_worker.h"

#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QFile>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QPushButton>
#include <QRadioButton>
#include <QSize>
#include <QTextEdit>
#include <QWidget>

class TitleBar;
class QSystemTrayIcon;

/**
 * @class MainWindow
 * @brief 无边框主窗口（自定义标题栏）。负责模式切换、业务控制与状态展示；解码后的画面推送到 ClientVideoWindow。
 *
 * 要点：客户端模式下主窗口仅保留左侧窄条，远程画面在独立顶层窗口展示；服务端已启动时禁止再次启动与切换运行模式；
 * 主窗口关闭（标题栏 ×、Alt+F4）结束整进程；最小化按钮将主窗口隐藏到系统托盘而非任务栏最小化。
 */
class MainWindow : public QMainWindow {
  Q_OBJECT
public:
  explicit MainWindow(QWidget* parent = nullptr);
  ~MainWindow() override;

protected:
  /** 顶层窗口收到关闭请求时结束整个 QApplication（配合 setQuitOnLastWindowClosed(false) 与托盘常驻）。 */
  void closeEvent(QCloseEvent* event) override;

private:
  /** 创建布局、控件与独立 ClientVideoWindow（无父对象，便于 Windows 任务栏独立显示）。 */
  void setupUi();
  /** 连接标题栏、托盘、ServerController、ClientController 及模式切换逻辑。 */
  void bindEvents();
  /** 带时间戳追加到界面日志区（与控制器 appendLog 信号配合）。 */
  void appendLog(const QString& text);
  /**
   * @brief 根据当前模式与 m_serverRunning / m_clientConnected 统一刷新各控件的 enabled 状态。
   * 在 stateChanged、启动/停止服务端、toggleMode 等路径调用，避免分散 setEnabled 导致状态不一致。
   */
  void updateControlAvailability();
  /** 初始化 QSystemTrayIcon、托盘菜单（显示/退出）与双击还原主窗口。 */
  void setupTrayIcon();
  /**
   * @brief 服务端模式使用固定窄宽高；客户端模式恢复为「仅左侧栏」宽度，远程画面不在主窗内。
   * @param isServerMode 当前是否为服务端单选状态
   */
  void updateWindowSizeByMode(bool isServerMode);
  /** 清晰度下拉框 → 协议枚举（发给客户端协商画质）。 */
  rdqt::QualityPreset currentPreset() const;
  /** 编码下拉框 → 协议枚举。 */
  rdqt::VideoCodec currentCodec() const;
  void openFileTransferDialog();
  void loadPersistedTransferState();
  void savePersistedTransferState() const;
  void clearPersistedTransferState() const;
  void tryResumePendingTransfer();
  /** 取消当前下载任务；如 removePartialFile=true，会删除本地未完成文件。 */
  void cancelDownloadTask(bool removePartialFile, const QString& reason);
  /** 取消当前上传任务；当前协议不删除远端残留文件，仅停止继续发送。 */
  void cancelUploadTask(const QString& reason);
  /** 根据最近一次分片完成情况计算瞬时速度，返回 bytes/s。 */
  double computeTransferSpeed(qint64 transferredBytes, qint64& sampleBytes, qint64& sampleMs) const;

private:
  struct DownloadTaskState {
    bool active = false;
    bool paused = false;
    QString remotePath;
    QString localPath;
    qint64 totalBytes = 0;
    qint64 transferredBytes = 0;
    qint64 speedSampleBytes = 0;
    qint64 speedSampleMs = 0;
    QFile file;
  };

  struct UploadTaskState {
    bool active = false;
    bool paused = false;
    QString localPath;
    QString remotePath;
    qint64 totalBytes = 0;
    qint64 transferredBytes = 0;
    qint64 speedSampleBytes = 0;
    qint64 speedSampleMs = 0;
    QFile file;
  };

  /** 服务端线程内工作与捕获逻辑封装。 */
  ServerController m_serverController;
  /** 客户端 TCP、解码与连接状态封装。 */
  ClientController m_clientController;
  QThread m_fileNetworkThread;
  TcpFileClientWorker* m_fileWorker = nullptr;

  TitleBar* m_titleBar = nullptr;
  QWidget* m_leftPanel = nullptr;

  QRadioButton* m_modeServer = nullptr;
  QRadioButton* m_modeClient = nullptr;
  QLineEdit* m_serverPortEdit = nullptr;
  QLabel* m_verifyCodeLabel = nullptr;
  QPushButton* m_startServerBtn = nullptr;
  QPushButton* m_stopServerBtn = nullptr;

  QLineEdit* m_clientIpEdit = nullptr;
  QLineEdit* m_clientPortEdit = nullptr;
  QLineEdit* m_clientCodeEdit = nullptr;
  QComboBox* m_qualityBox = nullptr;
  QComboBox* m_codecBox = nullptr;
  /** OpenGL / 软件渲染切换，与 ClientVideoWindow 内 QStackedWidget 页索引对应（0=OpenGL）。 */
  QComboBox* m_renderModeBox = nullptr;
  QPushButton* m_connectBtn = nullptr;
  QPushButton* m_disconnectBtn = nullptr;
  /** 可勾选：只看（不回传键鼠）或可控模式。 */
  QPushButton* m_controlModeBtn = nullptr;
  QLabel* m_qosServerLabel = nullptr;
  QLabel* m_qosClientLabel = nullptr;

  /**
   * 独立远程桌面顶层窗口：必须由 MainWindow 析构 delete（构造时使用 nullptr 父窗口）。
   * 不在此处作为 QObject 子节点，以便 Windows 任务栏单独条目。
   */
  ClientVideoWindow* m_clientVideoWindow = nullptr;
  /** 常驻托盘图标；主窗口 hide 后仍可通过托盘恢复或退出进程。 */
  QSystemTrayIcon* m_trayIcon = nullptr;
  FileTransferDialog* m_fileTransferDialog = nullptr;
  QTextEdit* m_logEdit = nullptr;
  /** 客户端模式下上次记录的主窗口尺寸（窄面板宽度），用于服务端 ⇄ 客户端切换时还原。 */
  QSize m_clientModeSize;
  /** TCP 已与远端握手成功且会话标记为连接中（用于启用断开、可控模式等）。 */
  bool m_clientConnected = false;
  /** 用户已点击「启动服务端」且尚未「停止」，用于禁用重复启动、监听端口编辑及运行模式切换。 */
  bool m_serverRunning = false;
  /** 仅在客户端可控模式下向服务端回传输入事件（键鼠）。 */
  bool m_remoteControlEnabled = false;
  /** 连接目标为本机环回或本机网卡地址时置 true（预留业务判断用）。 */
  bool m_isLoopbackTarget = false;
  DownloadTaskState m_downloadTask;
  UploadTaskState m_uploadTask;
};
