/**
 * @file mainwindow.cpp
 * @brief 主窗口实现：界面搭建、信号槽绑定、托盘与远程窗口交互、控件可用状态集中更新。
 */

#include "mainwindow.h"
#include "app_logger.h"
#include "title_bar.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QIcon>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHostAddress>
#include <QDir>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QNetworkInterface>
#include <QFile>
#include <QSaveFile>
#include <QStyle>
#include <QSystemTrayIcon>
#include <QVBoxLayout>

namespace {

constexpr qint64 kMaxSingleShotTransferBytes = 32LL * 1024 * 1024;
constexpr qint32 kFileChunkBytes = 256 * 1024;

QString transferStateFilePath() {
  const QString logDir = QCoreApplication::applicationDirPath() + "/logs";
  QDir().mkpath(logDir);
  return logDir + "/transfer_state.json";
}

/** 把 bytes/s 转成任务表可直接展示的中文速度文案。 */
QString transferSpeedText(double bytesPerSecond) {
  if (bytesPerSecond <= 0.0) {
    return QStringLiteral("-");
  }
  static const char* units[] = {"B/s", "KB/s", "MB/s", "GB/s"};
  double value = bytesPerSecond;
  int unitIndex = 0;
  while (value >= 1024.0 && unitIndex < 3) {
    value /= 1024.0;
    ++unitIndex;
  }
  if (unitIndex == 0) {
    return QStringLiteral("%1 %2").arg(static_cast<qint64>(value)).arg(QString::fromLatin1(units[unitIndex]));
  }
  return QStringLiteral("%1 %2").arg(QString::number(value, 'f', 1)).arg(QString::fromLatin1(units[unitIndex]));
}

}

// ---------------------------------------------------------------------------
// 构造 / 析构
// ---------------------------------------------------------------------------

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
  m_fileWorker = new TcpFileClientWorker();
  m_fileWorker->moveToThread(&m_fileNetworkThread);
  connect(&m_fileNetworkThread, &QThread::finished, m_fileWorker, &QObject::deleteLater);
  m_fileNetworkThread.start();
  loadPersistedTransferState();
  setupUi();
  bindEvents();
  // toggleMode() 在 bindEvents 末尾已执行一次，窗口宽度可能已从初始值收窄；此处记下用作客户端模式还原参照。
  m_clientModeSize = size();
}

MainWindow::~MainWindow() {
  if (m_fileWorker) {
    QMetaObject::invokeMethod(m_fileWorker, [this]() { m_fileWorker->disconnectHost(); }, Qt::QueuedConnection);
  }
  m_fileNetworkThread.quit();
  m_fileNetworkThread.wait();
  delete m_fileTransferDialog;
  m_fileTransferDialog = nullptr;
  // ClientVideoWindow 构造传入 nullptr，不参与 QObject 父子析构链，必须在主窗口销毁时手动释放。
  delete m_clientVideoWindow;
  m_clientVideoWindow = nullptr;
}

// ---------------------------------------------------------------------------
// setupUi：左侧控制面板 + 独立远程窗口（初始隐藏）
// ---------------------------------------------------------------------------

void MainWindow::setupUi() {
  setWindowTitle(QStringLiteral("远程桌面控制中心"));
  setWindowIcon(QIcon(QStringLiteral(":/icons/tray_icon.png")));
  resize(1300, 820);

  // FramelessWindowHint：去掉系统自带标题栏与边框，由 TitleBar 承担拖拽与最小化/最大化/关闭。
  setWindowFlags(Qt::Window | Qt::FramelessWindowHint);

  auto* center = new QWidget(this);
  setCentralWidget(center);

  auto* outer = new QVBoxLayout(center);
  outer->setContentsMargins(8, 8, 8, 8);
  outer->setSpacing(8);

  m_titleBar = new TitleBar(center);
  m_titleBar->setTitleText(windowTitle());
  outer->addWidget(m_titleBar);

  auto* content = new QWidget(center);
  outer->addWidget(content, 1);

  auto* root = new QHBoxLayout(content);
  root->setContentsMargins(4, 4, 4, 4);
  root->setSpacing(12);

  m_leftPanel = new QWidget(content);
  m_leftPanel->setFixedWidth(380);
  auto* leftLayout = new QVBoxLayout(m_leftPanel);
  leftLayout->setSpacing(10);

  auto* modeBox = new QGroupBox(QStringLiteral("运行模式"), m_leftPanel);
  auto* modeLayout = new QHBoxLayout(modeBox);
  m_modeServer = new QRadioButton(QStringLiteral("服务端"), modeBox);
  m_modeClient = new QRadioButton(QStringLiteral("客户端"), modeBox);
  m_modeServer->setChecked(true);
  modeLayout->addWidget(m_modeServer);
  modeLayout->addWidget(m_modeClient);

  auto* serverBox = new QGroupBox(QStringLiteral("服务端设置"), m_leftPanel);
  auto* serverLayout = new QFormLayout(serverBox);
  m_serverPortEdit = new QLineEdit(QStringLiteral("5555"), serverBox);
  m_verifyCodeLabel = new QLabel(QStringLiteral("未启动"), serverBox);
  m_startServerBtn = new QPushButton(QStringLiteral("启动服务端"), serverBox);
  m_stopServerBtn = new QPushButton(QStringLiteral("停止服务端"), serverBox);
  serverLayout->addRow(QStringLiteral("监听端口"), m_serverPortEdit);
  serverLayout->addRow(QStringLiteral("连接验证码"), m_verifyCodeLabel);
  serverLayout->addRow(m_startServerBtn);
  serverLayout->addRow(m_stopServerBtn);

  auto* clientBox = new QGroupBox(QStringLiteral("客户端连接"), m_leftPanel);
  auto* clientLayout = new QFormLayout(clientBox);
  m_clientIpEdit = new QLineEdit(QStringLiteral("127.0.0.1"), clientBox);
  m_clientPortEdit = new QLineEdit(QStringLiteral("5555"), clientBox);
  m_clientCodeEdit = new QLineEdit(clientBox);
  m_qualityBox = new QComboBox(clientBox);
  m_qualityBox->addItem(QStringLiteral("低清（低带宽）"));
  m_qualityBox->addItem(QStringLiteral("标清（平衡）"));
  m_qualityBox->addItem(QStringLiteral("高清（高质量）"));
  m_qualityBox->setCurrentIndex(1);
  m_codecBox = new QComboBox(clientBox);
  m_codecBox->addItem(QStringLiteral("自动（服务端协商）"));
  m_codecBox->addItem(QStringLiteral("H.264"));
  m_codecBox->addItem(QStringLiteral("JPEG"));
  m_codecBox->setCurrentIndex(0);
  m_renderModeBox = new QComboBox(clientBox);
  m_renderModeBox->addItem(QStringLiteral("OpenGL渲染"));
  m_renderModeBox->addItem(QStringLiteral("软件渲染"));
  m_connectBtn = new QPushButton(QStringLiteral("连接"), clientBox);
  m_disconnectBtn = new QPushButton(QStringLiteral("断开"), clientBox);
  m_controlModeBtn = new QPushButton(QStringLiteral("只看模式"), clientBox);
  m_controlModeBtn->setCheckable(true);
  m_controlModeBtn->setChecked(false);

  clientLayout->addRow(QStringLiteral("服务端 IP"), m_clientIpEdit);
  clientLayout->addRow(QStringLiteral("端口"), m_clientPortEdit);
  clientLayout->addRow(QStringLiteral("验证码"), m_clientCodeEdit);
  clientLayout->addRow(QStringLiteral("清晰度"), m_qualityBox);
  clientLayout->addRow(QStringLiteral("编码"), m_codecBox);
  clientLayout->addRow(QStringLiteral("渲染模式"), m_renderModeBox);
  clientLayout->addRow(m_connectBtn);
  clientLayout->addRow(m_disconnectBtn);
  clientLayout->addRow(m_controlModeBtn);

  auto* logBox = new QGroupBox(QStringLiteral("运行日志"), m_leftPanel);
  auto* logLayout = new QVBoxLayout(logBox);
  m_qosServerLabel = new QLabel(QStringLiteral("服务端 QoS: FPS=0, 码率=0 kbps, JPEG=0"), logBox);
  m_qosClientLabel = new QLabel(QStringLiteral("客户端 QoS: FPS=0, 码率=0 kbps, 重连=0, 延迟=-ms, 分辨率=-"), logBox);
  m_logEdit = new QTextEdit(logBox);
  m_logEdit->setReadOnly(true);
  logLayout->addWidget(m_qosServerLabel);
  logLayout->addWidget(m_qosClientLabel);
  logLayout->addWidget(m_logEdit);

  leftLayout->addWidget(modeBox);
  leftLayout->addWidget(serverBox);
  leftLayout->addWidget(clientBox);
  leftLayout->addWidget(logBox, 1);

  // parent 必须为 nullptr：若挂在 MainWindow 下，Windows 常把该顶层窗口当作附属窗口，任务栏无独立按钮。
  m_clientVideoWindow = new ClientVideoWindow(nullptr);
  m_clientVideoWindow->hide();

  root->addWidget(m_leftPanel);
}

// ---------------------------------------------------------------------------
// bindEvents：标题栏与托盘、服务端、客户端、画面与输入、模式切换
// ---------------------------------------------------------------------------

void MainWindow::bindEvents() {
  // 最小化：hide() 而非 showMinimized()，配合托盘常驻；不在任务栏保留最小化图标（符合「收到托盘」交互）。
  connect(m_titleBar, &TitleBar::minimizeRequested, this, [this]() {
    hide();
  });
  connect(m_titleBar, &TitleBar::maximizeRestoreRequested, this, [this]() {
    if (isMaximized()) {
      showNormal();
    } else {
      showMaximized();
    }
    m_titleBar->setMaximized(isMaximized());
  });
  // close → closeEvent → quit；Alt+F4 也会走 closeEvent。
  connect(m_titleBar, &TitleBar::closeRequested, this, &MainWindow::close);

  setupTrayIcon();

  // 若仍连接远端则弹框确认：同意则 stopConnect 并允许关闭远程窗口；取消则 neither 断开 nor 关闭窗口。
  m_clientVideoWindow->setCloseVerifier([this]() -> bool {
    if (!m_clientConnected) {
      return true;
    }
    const auto ans =
        QMessageBox::question(m_clientVideoWindow,
                              QStringLiteral("断开连接"),
                              QStringLiteral("关闭远程桌面窗口将断开与服务器的连接，是否继续？"),
                              QMessageBox::Yes | QMessageBox::No,
                              QMessageBox::No);
    if (ans != QMessageBox::Yes) {
      return false;
    }
    m_clientController.stopConnect();
    return true;
  });

  connect(&m_serverController, &ServerController::appendLog, this, &MainWindow::appendLog);
  connect(&m_clientController, &ClientController::appendLog, this, &MainWindow::appendLog);
  connect(m_fileWorker, &TcpFileClientWorker::logMessage, this, [this](const QString& text) {
    appendLog(QStringLiteral("文件通道：%1").arg(text));
  });
  connect(m_fileWorker, &TcpFileClientWorker::connected, this, [this](bool ok, const QString& reason) {
    // 文件通道断开时，把活动任务转为“暂停等待恢复”，避免主视频链路短暂波动后任务状态丢失。
    if (!m_fileTransferDialog) {
      if (ok) {
        tryResumePendingTransfer();
      } else {
        if (m_downloadTask.active) {
          m_downloadTask.paused = true;
        }
        if (m_uploadTask.active) {
          m_uploadTask.paused = true;
        }
        savePersistedTransferState();
      }
      return;
    }
    m_fileTransferDialog->setRemoteStatus(ok ? QStringLiteral("远程文件通道已连接")
                                             : QStringLiteral("远程文件通道状态：%1").arg(reason));
    if (!ok) {
      if (m_downloadTask.active) {
        m_downloadTask.paused = true;
      }
      if (m_uploadTask.active) {
        m_uploadTask.paused = true;
      }
      if (m_downloadTask.active) {
        m_fileTransferDialog->markDownloadProgress(
            m_downloadTask.remotePath,
            m_downloadTask.transferredBytes,
            m_downloadTask.totalBytes,
            QStringLiteral("下载已暂停：文件通道断开，等待恢复"));
        m_fileTransferDialog->updateTaskSpeed(m_downloadTask.remotePath, QStringLiteral("-"));
      }
      if (m_uploadTask.active) {
        m_fileTransferDialog->markUploadProgress(
            m_uploadTask.localPath,
            m_uploadTask.transferredBytes,
            m_uploadTask.totalBytes,
            QStringLiteral("上传已暂停：文件通道断开，等待恢复"));
        m_fileTransferDialog->updateTaskSpeed(m_uploadTask.localPath, QStringLiteral("-"));
      }
      savePersistedTransferState();
      m_fileTransferDialog->setRemoteRoots({});
      return;
    }
    tryResumePendingTransfer();
  });
  connect(m_fileWorker, &TcpFileClientWorker::rootsListed, this, [this](const QVector<rdqt::FileEntry>& entries) {
    if (m_fileTransferDialog) {
      m_fileTransferDialog->setRemoteRoots(entries);
    }
  });
  connect(m_fileWorker,
          &TcpFileClientWorker::directoryListed,
          this,
          [this](const QString& path, const QVector<rdqt::FileEntry>& entries) {
            if (m_fileTransferDialog) {
              m_fileTransferDialog->setRemoteDirectory(path, entries);
            }
          });
  connect(m_fileWorker,
          &TcpFileClientWorker::fileReceived,
          this,
          [this](const QString& remotePath, bool ok, const QString& reason, const QByteArray& data) {
            if (!m_fileTransferDialog) {
              return;
            }
            if (!ok) {
              m_fileTransferDialog->markDownloadFinished(remotePath, false, reason);
              appendLog(QStringLiteral("文件下载失败：%1").arg(reason));
              return;
            }
            if (data.size() > kMaxSingleShotTransferBytes) {
              const QString error = QStringLiteral("收到的文件超过 %1 MB，当前版本需等待分片下载实现。")
                                        .arg(kMaxSingleShotTransferBytes / (1024 * 1024));
              m_fileTransferDialog->markDownloadFinished(remotePath, false, error);
              appendLog(error);
              return;
            }
            const QFileInfo info(remotePath);
            const QString localDir = m_fileTransferDialog->property("pendingDownloadDir").toString();
            const QString localPath = QDir(localDir).filePath(info.fileName());
            const QFileInfo localInfo(localPath);
            if (localInfo.exists()) {
              const auto ans = QMessageBox::question(
                  m_fileTransferDialog,
                  QStringLiteral("覆盖本地文件"),
                  QStringLiteral("本地已存在同名文件，是否覆盖？\n%1").arg(QDir::toNativeSeparators(localPath)),
                  QMessageBox::Yes | QMessageBox::No,
                  QMessageBox::No);
              if (ans != QMessageBox::Yes) {
                m_fileTransferDialog->markDownloadFinished(remotePath, false, QStringLiteral("用户取消覆盖本地文件"));
                appendLog(QStringLiteral("已取消下载：本地文件已存在"));
                return;
              }
            }
            QFile file(localPath);
            if (!file.open(QIODevice::WriteOnly)) {
              const QString error = QStringLiteral("本地文件写入失败：%1").arg(file.errorString());
              m_fileTransferDialog->markDownloadFinished(remotePath, false, error);
              appendLog(error);
              return;
            }
            file.write(data);
            file.close();
            m_fileTransferDialog->markDownloadFinished(remotePath, true, QStringLiteral("已保存到 %1").arg(QDir::toNativeSeparators(localPath)));
            appendLog(QStringLiteral("文件已下载：%1").arg(QDir::toNativeSeparators(localPath)));
          });
  connect(m_fileWorker, &TcpFileClientWorker::fileWritten, this, [this](const QString& remotePath, bool ok, const QString& reason) {
    if (!m_fileTransferDialog) {
      return;
    }
    const QString localPath = m_fileTransferDialog->property("pendingUploadLocalPath").toString();
    if (!ok) {
      m_fileTransferDialog->markUploadFinished(localPath, false, reason);
      appendLog(QStringLiteral("文件上传失败：%1").arg(reason));
      return;
    }
    m_fileTransferDialog->markUploadFinished(
        localPath, true, QStringLiteral("已上传到 %1").arg(QDir::toNativeSeparators(remotePath)));
    appendLog(QStringLiteral("文件已上传：%1").arg(QDir::toNativeSeparators(remotePath)));
  });
  connect(m_fileWorker,
          &TcpFileClientWorker::fileChunkReceived,
          this,
          [this](const QString& remotePath,
                 qint64 totalSize,
                 qint64 offset,
                 bool done,
                 bool ok,
                 const QString& reason,
                 const QByteArray& data) {
            // 下载分片回包：
            // 1. 落盘到本地目标文件
            // 2. 更新断点续传状态与速度
            // 3. 若未完成则继续拉下一片；若用户已暂停则停在当前偏移等待恢复
            if (!m_fileTransferDialog || !m_downloadTask.active || remotePath != m_downloadTask.remotePath) {
              return;
            }
            if (!ok) {
              cancelDownloadTask(false, reason);
              return;
            }
            if (!m_downloadTask.file.isOpen()) {
              m_downloadTask.totalBytes = totalSize;
              m_downloadTask.file.setFileName(m_downloadTask.localPath);
              const QIODevice::OpenMode mode =
                  (m_downloadTask.transferredBytes > 0) ? QIODevice::ReadWrite : (QIODevice::WriteOnly | QIODevice::Truncate);
              if (!m_downloadTask.file.open(mode)) {
                const QString error = QStringLiteral("本地文件写入失败：%1").arg(m_downloadTask.file.errorString());
                cancelDownloadTask(false, error);
                return;
              }
            }
            if (!m_downloadTask.file.seek(offset) || m_downloadTask.file.write(data) != data.size()) {
              cancelDownloadTask(false, QStringLiteral("本地文件分片写入失败"));
              return;
            }
            m_downloadTask.transferredBytes = offset + data.size();
            const double speed =
                computeTransferSpeed(m_downloadTask.transferredBytes, m_downloadTask.speedSampleBytes, m_downloadTask.speedSampleMs);
            savePersistedTransferState();
            m_fileTransferDialog->markDownloadProgress(
                remotePath,
                m_downloadTask.transferredBytes,
                totalSize,
                QStringLiteral("下载中：%1 / %2")
                    .arg(m_downloadTask.transferredBytes)
                    .arg(totalSize));
            m_fileTransferDialog->updateTaskSpeed(remotePath, transferSpeedText(speed));
            if (done) {
              m_downloadTask.file.close();
              const QString finishedPath = m_downloadTask.localPath;
              m_downloadTask.active = false;
              m_downloadTask.paused = false;
              clearPersistedTransferState();
              m_fileTransferDialog->markDownloadFinished(
                  remotePath, true, QStringLiteral("已保存到 %1").arg(QDir::toNativeSeparators(finishedPath)));
              m_fileTransferDialog->updateTaskSpeed(remotePath, QStringLiteral("-"));
              appendLog(QStringLiteral("文件已下载：%1").arg(QDir::toNativeSeparators(finishedPath)));
              m_downloadTask.active = false;
              m_downloadTask.paused = false;
              m_downloadTask.remotePath.clear();
              m_downloadTask.localPath.clear();
              m_downloadTask.totalBytes = 0;
              m_downloadTask.transferredBytes = 0;
              m_downloadTask.speedSampleBytes = 0;
              m_downloadTask.speedSampleMs = 0;
              m_downloadTask.file.setFileName(QString());
              return;
            }
            if (m_downloadTask.paused) {
              m_fileTransferDialog->markDownloadProgress(
                  remotePath,
                  m_downloadTask.transferredBytes,
                  totalSize,
                  QStringLiteral("下载已暂停：%1 / %2").arg(m_downloadTask.transferredBytes).arg(totalSize));
              m_fileTransferDialog->updateTaskSpeed(remotePath, QStringLiteral("-"));
              return;
            }
            QMetaObject::invokeMethod(
                m_fileWorker,
                [this, remotePath]() {
                  m_fileWorker->requestFileChunk(remotePath, m_downloadTask.transferredBytes, kFileChunkBytes);
                },
                Qt::QueuedConnection);
          });
  connect(m_fileWorker,
          &TcpFileClientWorker::fileChunkWritten,
          this,
          [this](const QString& remotePath,
                 qint64 totalSize,
                 qint64 offset,
                 qint32 writtenBytes,
                 bool done,
                 bool ok,
                 const QString& reason) {
            // 上传分片确认：
            // 服务端每写完一块就回 ACK，客户端据此推进本地读取偏移和界面状态。
            if (!m_fileTransferDialog || !m_uploadTask.active || remotePath != m_uploadTask.remotePath) {
              return;
            }
            if (!ok) {
              cancelUploadTask(reason);
              return;
            }
            m_uploadTask.transferredBytes = offset + writtenBytes;
            const double speed =
                computeTransferSpeed(m_uploadTask.transferredBytes, m_uploadTask.speedSampleBytes, m_uploadTask.speedSampleMs);
            savePersistedTransferState();
            m_fileTransferDialog->markUploadProgress(
                m_uploadTask.localPath,
                m_uploadTask.transferredBytes,
                totalSize,
                QStringLiteral("上传中：%1 / %2").arg(m_uploadTask.transferredBytes).arg(totalSize));
            m_fileTransferDialog->updateTaskSpeed(m_uploadTask.localPath, transferSpeedText(speed));
            if (done) {
              m_uploadTask.file.close();
              const QString finishedLocalPath = m_uploadTask.localPath;
              m_uploadTask.active = false;
              m_uploadTask.paused = false;
              clearPersistedTransferState();
              m_fileTransferDialog->markUploadFinished(
                  finishedLocalPath, true, QStringLiteral("已上传到 %1").arg(QDir::toNativeSeparators(remotePath)));
              m_fileTransferDialog->updateTaskSpeed(finishedLocalPath, QStringLiteral("-"));
              appendLog(QStringLiteral("文件已上传：%1").arg(QDir::toNativeSeparators(remotePath)));
              m_uploadTask.active = false;
              m_uploadTask.paused = false;
              m_uploadTask.localPath.clear();
              m_uploadTask.remotePath.clear();
              m_uploadTask.totalBytes = 0;
              m_uploadTask.transferredBytes = 0;
              m_uploadTask.speedSampleBytes = 0;
              m_uploadTask.speedSampleMs = 0;
              m_uploadTask.file.setFileName(QString());
              return;
            }
            if (m_uploadTask.paused) {
              m_fileTransferDialog->markUploadProgress(
                  m_uploadTask.localPath,
                  m_uploadTask.transferredBytes,
                  totalSize,
                  QStringLiteral("上传已暂停：%1 / %2").arg(m_uploadTask.transferredBytes).arg(totalSize));
              m_fileTransferDialog->updateTaskSpeed(m_uploadTask.localPath, QStringLiteral("-"));
              return;
            }
            const QByteArray nextChunk = m_uploadTask.file.read(kFileChunkBytes);
            const qint64 nextOffset = m_uploadTask.transferredBytes;
            QMetaObject::invokeMethod(
                m_fileWorker,
                [this, remotePath, totalSize, nextOffset, nextChunk]() {
                  m_fileWorker->sendFileChunk(remotePath, totalSize, nextOffset, nextChunk);
                },
                Qt::QueuedConnection);
          });
  connect(m_fileWorker, &TcpFileClientWorker::pathDeleted, this, [this](const QString& remotePath, bool ok, const QString& reason) {
    if (!m_fileTransferDialog) {
      return;
    }
    if (ok) {
      appendLog(QStringLiteral("远程删除成功：%1").arg(QDir::toNativeSeparators(remotePath)));
      QMetaObject::invokeMethod(m_fileWorker, [this]() { m_fileWorker->requestRoots(); }, Qt::QueuedConnection);
      return;
    }
    QMessageBox::warning(
        m_fileTransferDialog, QStringLiteral("远程删除失败"), QStringLiteral("%1\n%2").arg(reason, QDir::toNativeSeparators(remotePath)));
    appendLog(QStringLiteral("远程删除失败：%1").arg(reason));
  });

  connect(m_startServerBtn, &QPushButton::clicked, this, [this]() {
    const quint16 port = m_serverPortEdit->text().toUShort();
    // 先置位并刷新控件，避免异步监听尚未就绪时用户重复点击启动。
    m_serverRunning = true;
    updateControlAvailability();
    m_serverController.startServer(port);
    m_verifyCodeLabel->setText(m_serverController.verifyCode());
    appendLog(QStringLiteral("服务端日志文件：%1").arg(applog::filePath(applog::Role::Server)));
  });

  connect(m_stopServerBtn, &QPushButton::clicked, this, [this]() {
    m_serverController.stopServer();
    m_serverRunning = false;
    updateControlAvailability();
    appendLog(QStringLiteral("服务端已停止"));
  });

  connect(m_connectBtn, &QPushButton::clicked, this, [this]() {
    const QString ip = m_clientIpEdit->text().trimmed();
    const quint16 port = m_clientPortEdit->text().toUShort();
    const QString code = m_clientCodeEdit->text().trimmed();
    if (ip.isEmpty() || code.isEmpty() || port == 0) {
      QMessageBox::warning(this, QStringLiteral("参数错误"), QStringLiteral("请完整填写 IP、端口和验证码。"));
      return;
    }

    auto isLoopbackOrLocal = [](const QString& host) -> bool {
      if (host.compare(QStringLiteral("localhost"), Qt::CaseInsensitive) == 0) {
        return true;
      }
      QHostAddress addr;
      if (!addr.setAddress(host)) {
        return false;
      }
      if (addr.isLoopback()) {
        return true;
      }
      const auto all = QNetworkInterface::allAddresses();
      return all.contains(addr);
    };
    // 判定连接目标是否为本机（环回或本机网卡 IP），供后续扩展策略（如同机操控时光标等）。
    m_isLoopbackTarget = isLoopbackOrLocal(ip);

    appendLog(QStringLiteral("客户端日志文件：%1").arg(applog::filePath(applog::Role::Client)));
    m_clientController.startConnect(ip, port, code, currentPreset(), currentCodec());
  });

  connect(m_disconnectBtn, &QPushButton::clicked, this, [this]() {
    m_clientController.stopConnect();
  });
  connect(m_controlModeBtn, &QPushButton::toggled, this, [this](bool checked) {
    m_remoteControlEnabled = checked;
    m_controlModeBtn->setText(checked ? QStringLiteral("可控模式") : QStringLiteral("只看模式"));
  });

  // 解码线程产出帧后主线程刷新 UI：仅更新当前渲染栈页面，减轻 GPU/CPU 双路径重复开销。
  connect(&m_clientController, &ClientController::frameUpdated, this, [this](const QImage& image) {
    if (image.isNull()) {
      return;
    }
    // StackedWidget 当前页索引与左侧「渲染模式」下拉框一致（0=OpenGL，1=软件）。
    if (m_clientVideoWindow->renderStack()->currentIndex() == 0) {
      m_clientVideoWindow->videoWidget()->setFrameImage(image);
    } else {
      m_clientVideoWindow->softwareWidget()->setFrameImage(image);
    }
  });
  connect(m_renderModeBox, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int idx) {
    m_clientVideoWindow->renderStack()->setCurrentIndex(idx == 0 ? 0 : 1);
    appendLog(idx == 0 ? QStringLiteral("已切换为 OpenGL 渲染") : QStringLiteral("已切换为软件渲染"));
  });

  // ok=true：链路就绪；ok=false：断开或失败。末尾统一 updateControlAvailability() 刷新连接/断开按钮等。
  connect(&m_clientController, &ClientController::stateChanged, this, [this](bool ok, const QString& reason) {
    m_clientConnected = ok;
    if (ok) {
      // 连接成功后默认开启可控模式，避免“已连接但输入未回传”的误解。
      m_remoteControlEnabled = true;
      m_controlModeBtn->setChecked(true);
      m_controlModeBtn->setText(QStringLiteral("可控模式"));
      if (m_modeClient->isChecked()) {
        m_clientVideoWindow->show();
        m_clientVideoWindow->raise();
        m_clientVideoWindow->activateWindow();
      }
      const QString ip = m_clientIpEdit->text().trimmed();
      const quint16 filePort = static_cast<quint16>(m_clientPortEdit->text().toUShort() + 1);
      const QString code = m_clientCodeEdit->text().trimmed();
      QMetaObject::invokeMethod(
          m_fileWorker,
          [this, ip, filePort, code]() { m_fileWorker->connectToHost(ip, filePort, code); },
          Qt::QueuedConnection);
    }
    if (!ok) {
      m_remoteControlEnabled = false;
      m_isLoopbackTarget = false;
      m_controlModeBtn->setChecked(false);
      m_controlModeBtn->setText(QStringLiteral("只看模式"));
      m_clientVideoWindow->hide();
      // 断开后强制恢复本地系统光标状态，避免出现残留的光标闪烁。
      m_clientVideoWindow->videoWidget()->clearFrame();
      m_clientVideoWindow->softwareWidget()->clearFrame();
      if (centralWidget()) {
        centralWidget()->setFocus();
      }
      QMetaObject::invokeMethod(m_fileWorker, [this]() { m_fileWorker->disconnectHost(); }, Qt::QueuedConnection);
    }
    appendLog(ok ? QStringLiteral("连接成功：%1").arg(reason) : QStringLiteral("连接状态：%1").arg(reason));
    updateControlAvailability();
  });
  connect(&m_serverController, &ServerController::qosUpdated, this, [this](int fps, int kbps, int jpeg) {
    m_qosServerLabel->setText(QStringLiteral("服务端 QoS: FPS=%1, 码率=%2 kbps, JPEG=%3").arg(fps).arg(kbps).arg(jpeg));
  });
  connect(&m_clientController,
          &ClientController::qosUpdated,
          this,
          [this](int fps, int kbps, int reconnectCount, int latencyMs, const QString& resolution) {
    m_qosClientLabel->setText(
        QStringLiteral("客户端 QoS: FPS=%1, 码率=%2 kbps, 重连=%3, 延迟=%4ms, 分辨率=%5")
            .arg(fps)
            .arg(kbps)
            .arg(reconnectCount)
            .arg(latencyMs < 0 ? QStringLiteral("-") : QString::number(latencyMs))
            .arg(resolution));
  });
  // 远程画面控件发出的输入事件统一转发；仅在客户端模式 + 已连接 + 可控模式下生效。
  auto inputHandler = [this](const rdqt::RemoteInputEvent& event) {
    if (!m_modeClient->isChecked() || !m_clientConnected || !m_remoteControlEnabled) {
      return;
    }
    m_clientController.sendRemoteInput(event);
  };
  connect(m_clientVideoWindow->videoWidget(), &VideoRenderWidget::inputEventGenerated, this, inputHandler);
  connect(m_clientVideoWindow->softwareWidget(), &SoftwareRenderWidget::inputEventGenerated, this, inputHandler);
  connect(m_clientVideoWindow, &ClientVideoWindow::transferRequested, this, &MainWindow::openFileTransferDialog);

  // 服务端 ⇄ 客户端：切换时收起远程窗口与最大化状态（服务端窄布局不支持最大化）、清空客户端连接标记。
  auto toggleMode = [this]() {
    const bool isServer = m_modeServer->isChecked();

    if (isServer) {
      m_clientVideoWindow->hide();
      m_clientVideoWindow->videoWidget()->clearFrame();
      m_clientVideoWindow->softwareWidget()->clearFrame();
      m_clientConnected = false;
      if (isMaximized()) {
        showNormal();
      }
    }
    updateWindowSizeByMode(isServer);
    if (m_titleBar) {
      m_titleBar->setMaximizeEnabled(!isServer);
      m_titleBar->setMaximized(isMaximized());
    }
    updateControlAvailability();
  };
  connect(m_modeServer, &QRadioButton::toggled, this, [toggleMode]() { toggleMode(); });
  toggleMode();
}

// ---------------------------------------------------------------------------
// 系统托盘：图标、右键菜单、双击还原主窗口
// ---------------------------------------------------------------------------

void MainWindow::setupTrayIcon() {
  QIcon icon(QStringLiteral(":/icons/tray_icon.png"));
  if (icon.isNull()) {
    icon = style()->standardIcon(QStyle::SP_ComputerIcon);
  }

  // QSystemTrayIcon 父对象为 MainWindow，主窗口销毁时一并删除图标。
  m_trayIcon = new QSystemTrayIcon(icon, this);
  m_trayIcon->setToolTip(QStringLiteral("远程桌面控制中心"));

  auto* trayMenu = new QMenu(this);
  QAction* showAct = trayMenu->addAction(QStringLiteral("显示控制中心"));
  connect(showAct, &QAction::triggered, this, [this]() {
    show();
    raise();
    activateWindow();
  });
  trayMenu->addSeparator();
  QAction* quitAct = trayMenu->addAction(QStringLiteral("退出"));
  connect(quitAct, &QAction::triggered, qApp, &QApplication::quit);

  m_trayIcon->setContextMenu(trayMenu);
  // Windows 下单击常以 Trigger 上报，此处按「双击」还原以免误触弹出主窗口。
  connect(m_trayIcon, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
    if (reason == QSystemTrayIcon::DoubleClick) {
      show();
      raise();
      activateWindow();
    }
  });

  if (!QSystemTrayIcon::isSystemTrayAvailable()) {
    appendLog(QStringLiteral("提示：当前环境无法使用系统托盘，隐藏主窗口后若需退出请使用任务管理器。"));
  }
  m_trayIcon->show();
}

void MainWindow::closeEvent(QCloseEvent* event) {
  // 与 setQuitOnLastWindowClosed(false) 配合：用户从标题栏关闭主窗时仍应结束进程，而非无声留驻后台。
  Q_UNUSED(event);
  QApplication::quit();
}

void MainWindow::loadPersistedTransferState() {
  // 启动时只恢复“任务存在”和“已传输到哪里”，真实恢复动作必须等主连接和文件通道都连好后再执行。
  QFile file(transferStateFilePath());
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return;
  }
  const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
  const QJsonObject root = doc.object();
  const QJsonObject download = root.value(QStringLiteral("download")).toObject();
  if (download.value(QStringLiteral("active")).toBool()) {
    m_downloadTask.active = true;
    m_downloadTask.paused = true;
    m_downloadTask.remotePath = download.value(QStringLiteral("remotePath")).toString();
    m_downloadTask.localPath = download.value(QStringLiteral("localPath")).toString();
    m_downloadTask.totalBytes = static_cast<qint64>(download.value(QStringLiteral("totalBytes")).toDouble());
    m_downloadTask.transferredBytes = static_cast<qint64>(download.value(QStringLiteral("transferredBytes")).toDouble());
    m_downloadTask.speedSampleBytes = m_downloadTask.transferredBytes;
    m_downloadTask.speedSampleMs = 0;
  }
  const QJsonObject upload = root.value(QStringLiteral("upload")).toObject();
  if (upload.value(QStringLiteral("active")).toBool()) {
    m_uploadTask.active = true;
    m_uploadTask.paused = true;
    m_uploadTask.localPath = upload.value(QStringLiteral("localPath")).toString();
    m_uploadTask.remotePath = upload.value(QStringLiteral("remotePath")).toString();
    m_uploadTask.totalBytes = static_cast<qint64>(upload.value(QStringLiteral("totalBytes")).toDouble());
    m_uploadTask.transferredBytes = static_cast<qint64>(upload.value(QStringLiteral("transferredBytes")).toDouble());
    m_uploadTask.speedSampleBytes = m_uploadTask.transferredBytes;
    m_uploadTask.speedSampleMs = 0;
  }
}

void MainWindow::savePersistedTransferState() const {
  QJsonObject root;
  QJsonObject download;
  download.insert(QStringLiteral("active"), m_downloadTask.active);
  download.insert(QStringLiteral("remotePath"), m_downloadTask.remotePath);
  download.insert(QStringLiteral("localPath"), m_downloadTask.localPath);
  download.insert(QStringLiteral("totalBytes"), static_cast<double>(m_downloadTask.totalBytes));
  download.insert(QStringLiteral("transferredBytes"), static_cast<double>(m_downloadTask.transferredBytes));
  root.insert(QStringLiteral("download"), download);

  QJsonObject upload;
  upload.insert(QStringLiteral("active"), m_uploadTask.active);
  upload.insert(QStringLiteral("localPath"), m_uploadTask.localPath);
  upload.insert(QStringLiteral("remotePath"), m_uploadTask.remotePath);
  upload.insert(QStringLiteral("totalBytes"), static_cast<double>(m_uploadTask.totalBytes));
  upload.insert(QStringLiteral("transferredBytes"), static_cast<double>(m_uploadTask.transferredBytes));
  root.insert(QStringLiteral("upload"), upload);

  QSaveFile file(transferStateFilePath());
  if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
    return;
  }
  file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
  file.commit();
}

void MainWindow::clearPersistedTransferState() const {
  // 上传和下载状态共用同一个 JSON 文件。
  // 因此只要另一侧任务仍然活跃，就不能直接删除整份文件，而是要把剩余任务重新写回去。
  if (m_downloadTask.active || m_uploadTask.active) {
    savePersistedTransferState();
    return;
  }
  QFile::remove(transferStateFilePath());
}

double MainWindow::computeTransferSpeed(qint64 transferredBytes, qint64& sampleBytes, qint64& sampleMs) const {
  const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
  if (sampleMs <= 0) {
    sampleMs = nowMs;
    sampleBytes = transferredBytes;
    return 0.0;
  }
  const qint64 deltaMs = nowMs - sampleMs;
  const qint64 deltaBytes = transferredBytes - sampleBytes;
  if (deltaMs <= 0 || deltaBytes <= 0) {
    return 0.0;
  }
  sampleMs = nowMs;
  sampleBytes = transferredBytes;
  return static_cast<double>(deltaBytes) * 1000.0 / static_cast<double>(deltaMs);
}

void MainWindow::cancelDownloadTask(bool removePartialFile, const QString& reason) {
  if (!m_downloadTask.active) {
    return;
  }
  if (m_downloadTask.file.isOpen()) {
    m_downloadTask.file.close();
  }
  const QString localPath = m_downloadTask.localPath;
  const QString remotePath = m_downloadTask.remotePath;
  m_downloadTask.active = false;
  m_downloadTask.paused = false;
  m_downloadTask.remotePath.clear();
  m_downloadTask.localPath.clear();
  m_downloadTask.totalBytes = 0;
  m_downloadTask.transferredBytes = 0;
  m_downloadTask.speedSampleBytes = 0;
  m_downloadTask.speedSampleMs = 0;
  m_downloadTask.file.setFileName(QString());
  clearPersistedTransferState();
  if (removePartialFile && !localPath.isEmpty()) {
    QFile::remove(localPath);
  }
  if (m_fileTransferDialog) {
    m_fileTransferDialog->markDownloadFinished(remotePath, false, reason);
    m_fileTransferDialog->updateTaskSpeed(remotePath, QStringLiteral("-"));
  }
  appendLog(QStringLiteral("下载任务已取消：%1").arg(reason));
}

void MainWindow::cancelUploadTask(const QString& reason) {
  if (!m_uploadTask.active) {
    return;
  }
  if (m_uploadTask.file.isOpen()) {
    m_uploadTask.file.close();
  }
  const QString localPath = m_uploadTask.localPath;
  m_uploadTask.active = false;
  m_uploadTask.paused = false;
  m_uploadTask.localPath.clear();
  m_uploadTask.remotePath.clear();
  m_uploadTask.totalBytes = 0;
  m_uploadTask.transferredBytes = 0;
  m_uploadTask.speedSampleBytes = 0;
  m_uploadTask.speedSampleMs = 0;
  m_uploadTask.file.setFileName(QString());
  clearPersistedTransferState();
  if (m_fileTransferDialog) {
    m_fileTransferDialog->markUploadFinished(localPath, false, reason);
    m_fileTransferDialog->updateTaskSpeed(localPath, QStringLiteral("-"));
  }
  appendLog(QStringLiteral("上传任务已取消：%1").arg(reason));
}

void MainWindow::tryResumePendingTransfer() {
  if (!m_clientConnected) {
    return;
  }
  // 只要文件通道恢复，优先恢复断点续传任务；下载与上传当前同一时刻仅允许一个活跃任务。
  if (m_downloadTask.active && m_downloadTask.paused && !m_downloadTask.remotePath.isEmpty()) {
    m_downloadTask.paused = false;
    m_downloadTask.speedSampleBytes = m_downloadTask.transferredBytes;
    m_downloadTask.speedSampleMs = 0;
    savePersistedTransferState();
    if (m_fileTransferDialog) {
      m_fileTransferDialog->markDownloadProgress(
          m_downloadTask.remotePath,
          m_downloadTask.transferredBytes,
          m_downloadTask.totalBytes,
          QStringLiteral("下载恢复中：%1 / %2").arg(m_downloadTask.transferredBytes).arg(m_downloadTask.totalBytes));
      m_fileTransferDialog->updateTaskSpeed(m_downloadTask.remotePath, QStringLiteral("-"));
    }
    QMetaObject::invokeMethod(
        m_fileWorker,
        [this]() {
          m_fileWorker->requestFileChunk(m_downloadTask.remotePath, m_downloadTask.transferredBytes, kFileChunkBytes);
        },
        Qt::QueuedConnection);
    return;
  }
  if (m_uploadTask.active && m_uploadTask.paused && !m_uploadTask.localPath.isEmpty()) {
    m_uploadTask.paused = false;
    m_uploadTask.speedSampleBytes = m_uploadTask.transferredBytes;
    m_uploadTask.speedSampleMs = 0;
    savePersistedTransferState();
    if (!m_uploadTask.file.isOpen()) {
      m_uploadTask.file.setFileName(m_uploadTask.localPath);
      if (!m_uploadTask.file.open(QIODevice::ReadOnly)) {
        appendLog(QStringLiteral("恢复上传失败：无法重新打开本地文件"));
        m_uploadTask.active = false;
        clearPersistedTransferState();
        return;
      }
    }
    m_uploadTask.file.seek(m_uploadTask.transferredBytes);
    const QByteArray nextChunk = m_uploadTask.file.read(kFileChunkBytes);
    if (m_fileTransferDialog) {
      m_fileTransferDialog->markUploadProgress(
          m_uploadTask.localPath,
          m_uploadTask.transferredBytes,
          m_uploadTask.totalBytes,
          QStringLiteral("上传恢复中：%1 / %2").arg(m_uploadTask.transferredBytes).arg(m_uploadTask.totalBytes));
      m_fileTransferDialog->updateTaskSpeed(m_uploadTask.localPath, QStringLiteral("-"));
    }
    QMetaObject::invokeMethod(
        m_fileWorker,
        [this, nextChunk]() {
          m_fileWorker->sendFileChunk(m_uploadTask.remotePath, m_uploadTask.totalBytes, m_uploadTask.transferredBytes, nextChunk);
        },
        Qt::QueuedConnection);
  }
}

void MainWindow::openFileTransferDialog() {
  if (!m_fileTransferDialog) {
    m_fileTransferDialog = new FileTransferDialog(this);
    connect(m_fileTransferDialog, &FileTransferDialog::remoteRootsRequested, this, [this]() {
      QMetaObject::invokeMethod(m_fileWorker, [this]() { m_fileWorker->requestRoots(); }, Qt::QueuedConnection);
    });
    connect(m_fileTransferDialog, &FileTransferDialog::remoteDirectoryRequested, this, [this](const QString& path) {
      QMetaObject::invokeMethod(m_fileWorker, [this, path]() { m_fileWorker->requestDirectory(path); }, Qt::QueuedConnection);
    });
    connect(m_fileTransferDialog, &FileTransferDialog::remoteDeleteRequested, this, [this](const QString& path, bool recursive) {
      QMetaObject::invokeMethod(
          m_fileWorker, [this, path, recursive]() { m_fileWorker->requestDeletePath(path, recursive); }, Qt::QueuedConnection);
    });
    connect(m_fileTransferDialog,
            &FileTransferDialog::remoteFileDownloadRequested,
            this,
            [this](const QString& remotePath, const QString& localDir, qint64 totalBytes) {
              // 当前版本任务管理按“同一时刻一个下载 + 一个上传”设计，重复发起时先终止旧任务，避免状态交叉。
              if (m_downloadTask.active) {
                cancelDownloadTask(true, QStringLiteral("已被新的下载任务替换"));
              }
              const QString localPath = QDir(localDir).filePath(QFileInfo(remotePath).fileName());
              const QFileInfo localInfo(localPath);
              if (localInfo.exists()) {
                const auto ans = QMessageBox::question(
                    m_fileTransferDialog,
                    QStringLiteral("覆盖本地文件"),
                    QStringLiteral("本地已存在同名文件，是否覆盖？\n%1").arg(QDir::toNativeSeparators(localPath)),
                    QMessageBox::Yes | QMessageBox::No,
                    QMessageBox::No);
                if (ans != QMessageBox::Yes) {
                  m_fileTransferDialog->markDownloadFinished(remotePath, false, QStringLiteral("用户取消覆盖本地文件"));
                  appendLog(QStringLiteral("已取消下载：本地文件已存在"));
                  return;
                }
              }
              m_downloadTask.active = true;
              m_downloadTask.paused = false;
              m_downloadTask.remotePath = remotePath;
              m_downloadTask.localPath = localPath;
              m_downloadTask.totalBytes = totalBytes;
              m_downloadTask.transferredBytes = 0;
              m_downloadTask.speedSampleBytes = 0;
              m_downloadTask.speedSampleMs = 0;
              if (m_downloadTask.file.isOpen()) {
                m_downloadTask.file.close();
              }
              m_downloadTask.file.setFileName(localPath);
              savePersistedTransferState();
              m_fileTransferDialog->markDownloadStarted(remotePath, localDir, totalBytes);
              m_fileTransferDialog->updateTaskSpeed(remotePath, QStringLiteral("-"));
              QMetaObject::invokeMethod(
                  m_fileWorker,
                  [this, remotePath]() { m_fileWorker->requestFileChunk(remotePath, 0, kFileChunkBytes); },
                  Qt::QueuedConnection);
            });
    connect(m_fileTransferDialog,
            &FileTransferDialog::localFileUploadRequested,
            this,
            [this](const QString& localPath, const QString& remoteDir) {
              if (m_uploadTask.active) {
                cancelUploadTask(QStringLiteral("已被新的上传任务替换"));
              }
              QFileInfo localInfo(localPath);
              if (localInfo.size() <= 0) {
                m_fileTransferDialog->markUploadFinished(
                    localPath, false, QStringLiteral("本地文件不存在或为空，无法上传"));
                appendLog(QStringLiteral("文件上传失败：本地文件不存在或为空"));
                return;
              }
              const QString remotePath = QDir(remoteDir).filePath(QFileInfo(localPath).fileName());
              const auto ans = QMessageBox::question(
                  m_fileTransferDialog,
                  QStringLiteral("上传到远程"),
                  QStringLiteral("将把文件上传到远程目录：\n%1\n\n是否继续？").arg(QDir::toNativeSeparators(remotePath)),
                  QMessageBox::Yes | QMessageBox::No,
                  QMessageBox::Yes);
              if (ans != QMessageBox::Yes) {
                m_fileTransferDialog->markUploadFinished(localPath, false, QStringLiteral("用户取消上传"));
                appendLog(QStringLiteral("已取消上传：%1").arg(QDir::toNativeSeparators(localPath)));
                return;
              }
              m_uploadTask.active = true;
              m_uploadTask.paused = false;
              m_uploadTask.localPath = localPath;
              m_uploadTask.remotePath = remotePath;
              m_uploadTask.totalBytes = localInfo.size();
              m_uploadTask.transferredBytes = 0;
              m_uploadTask.speedSampleBytes = 0;
              m_uploadTask.speedSampleMs = 0;
              if (m_uploadTask.file.isOpen()) {
                m_uploadTask.file.close();
              }
              m_uploadTask.file.setFileName(localPath);
              if (!m_uploadTask.file.open(QIODevice::ReadOnly)) {
                m_uploadTask.active = false;
                m_fileTransferDialog->markUploadFinished(
                    localPath, false, QStringLiteral("本地文件读取失败：%1").arg(m_uploadTask.file.errorString()));
                return;
              }
              const QByteArray firstChunk = m_uploadTask.file.read(kFileChunkBytes);
              savePersistedTransferState();
              m_fileTransferDialog->markUploadStarted(localPath, remoteDir, m_uploadTask.totalBytes);
              m_fileTransferDialog->updateTaskSpeed(localPath, QStringLiteral("-"));
              QMetaObject::invokeMethod(
                  m_fileWorker,
                  [this, remotePath, firstChunk]() {
                    m_fileWorker->sendFileChunk(remotePath, m_uploadTask.totalBytes, 0, firstChunk);
                  },
                  Qt::QueuedConnection);
            });
    connect(m_fileTransferDialog,
            &FileTransferDialog::taskActionRequested,
            this,
            [this](const QString& action, const QString& taskKey) {
              // 右键菜单按任务主键路由：下载任务主键为 remotePath，上传任务主键为 localPath。
              if (m_downloadTask.active && taskKey == m_downloadTask.remotePath) {
                if (action == QStringLiteral("cancel")) {
                  cancelDownloadTask(true, QStringLiteral("用户在任务列表中取消下载"));
                  return;
                }
                if (action == QStringLiteral("pause") && !m_downloadTask.paused) {
                  m_downloadTask.paused = true;
                  savePersistedTransferState();
                  m_fileTransferDialog->markDownloadProgress(
                      m_downloadTask.remotePath,
                      m_downloadTask.transferredBytes,
                      m_downloadTask.totalBytes,
                      QStringLiteral("下载已暂停：%1 / %2").arg(m_downloadTask.transferredBytes).arg(m_downloadTask.totalBytes));
                  m_fileTransferDialog->updateTaskSpeed(m_downloadTask.remotePath, QStringLiteral("-"));
                  return;
                }
                if (action == QStringLiteral("resume") && m_downloadTask.paused) {
                  m_downloadTask.paused = false;
                  m_downloadTask.speedSampleBytes = m_downloadTask.transferredBytes;
                  m_downloadTask.speedSampleMs = 0;
                  savePersistedTransferState();
                  m_fileTransferDialog->markDownloadProgress(
                      m_downloadTask.remotePath,
                      m_downloadTask.transferredBytes,
                      m_downloadTask.totalBytes,
                      QStringLiteral("下载恢复中：%1 / %2").arg(m_downloadTask.transferredBytes).arg(m_downloadTask.totalBytes));
                  QMetaObject::invokeMethod(
                      m_fileWorker,
                      [this]() {
                        m_fileWorker->requestFileChunk(m_downloadTask.remotePath, m_downloadTask.transferredBytes, kFileChunkBytes);
                      },
                      Qt::QueuedConnection);
                }
                return;
              }
              if (m_uploadTask.active && taskKey == m_uploadTask.localPath) {
                if (action == QStringLiteral("cancel")) {
                  cancelUploadTask(QStringLiteral("用户在任务列表中取消上传"));
                  return;
                }
                if (action == QStringLiteral("pause") && !m_uploadTask.paused) {
                  m_uploadTask.paused = true;
                  savePersistedTransferState();
                  m_fileTransferDialog->markUploadProgress(
                      m_uploadTask.localPath,
                      m_uploadTask.transferredBytes,
                      m_uploadTask.totalBytes,
                      QStringLiteral("上传已暂停：%1 / %2").arg(m_uploadTask.transferredBytes).arg(m_uploadTask.totalBytes));
                  m_fileTransferDialog->updateTaskSpeed(m_uploadTask.localPath, QStringLiteral("-"));
                  return;
                }
                if (action == QStringLiteral("resume") && m_uploadTask.paused) {
                  m_uploadTask.paused = false;
                  m_uploadTask.speedSampleBytes = m_uploadTask.transferredBytes;
                  m_uploadTask.speedSampleMs = 0;
                  savePersistedTransferState();
                  const QByteArray nextChunk = m_uploadTask.file.read(kFileChunkBytes);
                  const qint64 nextOffset = m_uploadTask.transferredBytes;
                  m_fileTransferDialog->markUploadProgress(
                      m_uploadTask.localPath,
                      m_uploadTask.transferredBytes,
                      m_uploadTask.totalBytes,
                      QStringLiteral("上传恢复中：%1 / %2").arg(m_uploadTask.transferredBytes).arg(m_uploadTask.totalBytes));
                  QMetaObject::invokeMethod(
                      m_fileWorker,
                      [this, nextChunk, nextOffset]() {
                        m_fileWorker->sendFileChunk(m_uploadTask.remotePath, m_uploadTask.totalBytes, nextOffset, nextChunk);
                      },
                      Qt::QueuedConnection);
                }
              }
            });
  }
  if (m_clientConnected) {
    QMetaObject::invokeMethod(m_fileWorker, [this]() { m_fileWorker->requestRoots(); }, Qt::QueuedConnection);
    tryResumePendingTransfer();
  } else {
    m_fileTransferDialog->setRemoteStatus(QStringLiteral("请先建立远程桌面连接，再浏览远程目录"));
  }
  // 改为模态执行：弹窗打开期间阻止用户继续操作主窗口/远程画面窗口，避免状态交叉。
  m_fileTransferDialog->exec();
}

// ---------------------------------------------------------------------------
// 协议参数：由 UI 下拉框映射到传输层使用的枚举
// ---------------------------------------------------------------------------

rdqt::QualityPreset MainWindow::currentPreset() const {
  switch (m_qualityBox->currentIndex()) {
    case 0:
      return rdqt::QualityPreset::Low;
    case 2:
      return rdqt::QualityPreset::High;
    default:
      return rdqt::QualityPreset::Medium;
  }
}

rdqt::VideoCodec MainWindow::currentCodec() const {
  switch (m_codecBox ? m_codecBox->currentIndex() : 0) {
    case 1:
      return rdqt::VideoCodec::H264;
    case 2:
      return rdqt::VideoCodec::Jpeg;
    default:
      return rdqt::VideoCodec::Auto;
  }
}

void MainWindow::updateControlAvailability() {
  const bool isServer = m_modeServer->isChecked();

  // 服务端监听未停止前禁止切换「服务端/客户端」，防止双端并发与状态错乱。
  m_modeServer->setEnabled(!m_serverRunning);
  m_modeClient->setEnabled(!m_serverRunning);

  m_startServerBtn->setEnabled(isServer && !m_serverRunning);
  m_stopServerBtn->setEnabled(isServer && m_serverRunning);
  m_serverPortEdit->setEnabled(isServer && !m_serverRunning);

  m_clientIpEdit->setEnabled(!isServer);
  m_clientPortEdit->setEnabled(!isServer);
  m_clientCodeEdit->setEnabled(!isServer);
  m_qualityBox->setEnabled(!isServer);
  m_codecBox->setEnabled(!isServer);
  m_renderModeBox->setEnabled(!isServer);
  m_connectBtn->setEnabled(!isServer && !m_clientConnected);
  m_disconnectBtn->setEnabled(!isServer && m_clientConnected);
  // 未连接成功时无可控/只看语义，控件置灰由 isServer 与 m_clientConnected 共同决定。
  m_controlModeBtn->setEnabled(!isServer && m_clientConnected);
}

void MainWindow::appendLog(const QString& text) {
  const QString ts = QDateTime::currentDateTime().toString("HH:mm:ss");
  m_logEdit->append(QStringLiteral("[%1] %2").arg(ts, text));
}

// ---------------------------------------------------------------------------
// 窗口尺寸策略：服务端固定窄宽；客户端为左侧面板宽度（画面在 ClientVideoWindow）
// ---------------------------------------------------------------------------

void MainWindow::updateWindowSizeByMode(bool isServerMode) {
  if (isServerMode) {
    // 从客户端控制面板窄窗口切到服务端前保存尺寸，便于再次切回客户端时还原。
    if (width() >= 380 && width() <= 520) {
      m_clientModeSize = size();
    }
    const int serverWidth = 430;
    setMinimumWidth(serverWidth);
    setMaximumWidth(serverWidth);
    setMinimumHeight(760);
    resize(serverWidth, qMax(760, height()));
  } else {
    // 客户端模式：主窗口仅左侧控制栏，画面在独立窗口。
    const QSize fallbackClientSize(430, 760);
    const QSize target = (m_clientModeSize.width() >= 380 && m_clientModeSize.width() <= 520) ? m_clientModeSize
                                                                                             : fallbackClientSize;
    setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    setMinimumWidth(430);
    setMinimumHeight(760);
    resize(target);
  }
}
