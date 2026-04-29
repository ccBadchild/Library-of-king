#include "mainwindow.h"
#include "app_logger.h"
#include "title_bar.h"

#include <QDateTime>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHostAddress>
#include <QLabel>
#include <QMessageBox>
#include <QNetworkInterface>
#include <QVBoxLayout>

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
  setupUi();
  bindEvents();
  m_clientModeSize = size();
}

void MainWindow::setupUi() {
  setWindowTitle(QStringLiteral("远程桌面控制中心"));
  resize(1300, 820);

  // 去除系统标题栏，使用自定义标题栏。
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

  m_leftPanelHost = new QWidget(content);
  auto* leftHostLayout = new QHBoxLayout(m_leftPanelHost);
  leftHostLayout->setContentsMargins(0, 0, 0, 0);
  leftHostLayout->setSpacing(8);

  m_leftPanel = new QWidget(m_leftPanelHost);
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

  m_leftPanelToggleBtn = new QPushButton(QStringLiteral("◀"), m_leftPanelHost);
  m_leftPanelToggleBtn->setObjectName(QStringLiteral("leftPanelToggleBtn"));
  m_leftPanelToggleBtn->setFixedWidth(28);
  m_leftPanelToggleBtn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
  m_leftPanelToggleBtn->setToolTip(QStringLiteral("收起左侧控制栏"));

  leftHostLayout->addWidget(m_leftPanel);
  leftHostLayout->addWidget(m_leftPanelToggleBtn);

  m_remoteDesktopBox = new QGroupBox(QStringLiteral("远程桌面"), center);
  auto* rightLayout = new QVBoxLayout(m_remoteDesktopBox);
  m_renderStack = new QStackedWidget(m_remoteDesktopBox);
  m_videoWidget = new VideoRenderWidget(m_remoteDesktopBox);
  m_videoWidget->setMinimumSize(800, 600);
  m_softwareWidget = new SoftwareRenderWidget(m_remoteDesktopBox);
  m_softwareWidget->setMinimumSize(800, 600);
  m_renderStack->addWidget(m_videoWidget);
  m_renderStack->addWidget(m_softwareWidget);
  m_renderStack->setCurrentWidget(m_videoWidget);
  rightLayout->addWidget(m_renderStack, 1);

  root->addWidget(m_leftPanelHost);
  root->addWidget(m_remoteDesktopBox, 1);
}

void MainWindow::bindEvents() {
  connect(m_titleBar, &TitleBar::minimizeRequested, this, [this]() { showMinimized(); });
  connect(m_titleBar, &TitleBar::maximizeRestoreRequested, this, [this]() {
    if (isMaximized()) {
      showNormal();
    } else {
      showMaximized();
    }
    m_titleBar->setMaximized(isMaximized());
  });
  connect(m_titleBar, &TitleBar::closeRequested, this, [this]() { close(); });
  connect(m_leftPanelToggleBtn, &QPushButton::clicked, this, [this]() {
    m_leftPanelCollapsed = !m_leftPanelCollapsed;
    if (m_leftPanel) {
      m_leftPanel->setVisible(!m_leftPanelCollapsed);
    }
    if (m_leftPanelToggleBtn) {
      m_leftPanelToggleBtn->setText(m_leftPanelCollapsed ? QStringLiteral("▶") : QStringLiteral("◀"));
      m_leftPanelToggleBtn->setToolTip(m_leftPanelCollapsed ? QStringLiteral("展开左侧控制栏")
                                                            : QStringLiteral("收起左侧控制栏"));
    }
  });

  connect(&m_serverController, &ServerController::appendLog, this, &MainWindow::appendLog);
  connect(&m_clientController, &ClientController::appendLog, this, &MainWindow::appendLog);

  connect(m_startServerBtn, &QPushButton::clicked, this, [this]() {
    const quint16 port = m_serverPortEdit->text().toUShort();
    m_serverController.startServer(port);
    m_verifyCodeLabel->setText(m_serverController.verifyCode());
    appendLog(QStringLiteral("服务端日志文件：%1").arg(applog::filePath(applog::Role::Server)));
  });

  connect(m_stopServerBtn, &QPushButton::clicked, this, [this]() {
    m_serverController.stopServer();
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
    m_isLoopbackTarget = isLoopbackOrLocal(ip);

    appendLog(QStringLiteral("客户端日志文件：%1").arg(applog::filePath(applog::Role::Client)));
    // 仅在发起客户端连接时显示远程桌面区域，断开后将再次隐藏。
    m_remoteDesktopBox->setVisible(true);
    m_clientController.startConnect(ip, port, code, currentPreset(), currentCodec());
  });

  connect(m_disconnectBtn, &QPushButton::clicked, this, [this]() {
    m_clientController.stopConnect();
  });
  connect(m_controlModeBtn, &QPushButton::toggled, this, [this](bool checked) {
    m_remoteControlEnabled = checked;
    m_controlModeBtn->setText(checked ? QStringLiteral("可控模式") : QStringLiteral("只看模式"));
  });

  connect(&m_clientController, &ClientController::frameUpdated, this, [this](const QImage& image) {
    if (image.isNull()) {
      return;
    }
    m_videoWidget->setFrameImage(image);
    m_softwareWidget->setFrameImage(image);
  });
  connect(m_renderModeBox, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int idx) {
    m_renderStack->setCurrentIndex(idx == 0 ? 0 : 1);
    appendLog(idx == 0 ? QStringLiteral("已切换为 OpenGL 渲染") : QStringLiteral("已切换为软件渲染"));
  });

  connect(&m_clientController, &ClientController::stateChanged, this, [this](bool ok, const QString& reason) {
    m_clientConnected = ok;
    m_controlModeBtn->setEnabled(m_modeClient->isChecked() && m_clientConnected);
    // 连接成功后禁用“连接”，仅允许“断开”；断开后恢复。
    const bool inClientMode = m_modeClient->isChecked();
    m_connectBtn->setEnabled(inClientMode && !m_clientConnected);
    m_disconnectBtn->setEnabled(inClientMode && m_clientConnected);
    if (ok) {
      // 连接成功后默认开启可控模式，避免“已连接但输入未回传”的误解。
      m_remoteControlEnabled = true;
      m_controlModeBtn->setChecked(true);
      m_controlModeBtn->setText(QStringLiteral("可控模式"));
    }
    if (!ok) {
      m_remoteControlEnabled = false;
      m_isLoopbackTarget = false;
      m_controlModeBtn->setChecked(false);
      m_controlModeBtn->setText(QStringLiteral("只看模式"));
      // 断开后强制恢复本地系统光标状态，避免出现残留的光标闪烁。
      m_videoWidget->clearFrame();
      m_softwareWidget->clearFrame();
      // 客户端模式下断开连接后仍保留远程桌面区域，仅清空为等待状态。
      m_remoteDesktopBox->setVisible(m_modeClient->isChecked());
      if (centralWidget()) {
        centralWidget()->setFocus();
      }
    }
    appendLog(ok ? QStringLiteral("连接成功：%1").arg(reason) : QStringLiteral("连接状态：%1").arg(reason));
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
  auto inputHandler = [this](const rdqt::RemoteInputEvent& event) {
    if (!m_modeClient->isChecked() || !m_clientConnected || !m_remoteControlEnabled) {
      return;
    }
    m_clientController.sendRemoteInput(event);
  };
  connect(m_videoWidget, &VideoRenderWidget::inputEventGenerated, this, inputHandler);
  connect(m_softwareWidget, &SoftwareRenderWidget::inputEventGenerated, this, inputHandler);

  auto toggleMode = [this]() {
    const bool isServer = m_modeServer->isChecked();
    m_startServerBtn->setEnabled(isServer);
    m_stopServerBtn->setEnabled(isServer);
    m_serverPortEdit->setEnabled(isServer);

    m_clientIpEdit->setEnabled(!isServer);
    m_clientPortEdit->setEnabled(!isServer);
    m_clientCodeEdit->setEnabled(!isServer);
    m_qualityBox->setEnabled(!isServer);
    m_codecBox->setEnabled(!isServer);
    m_connectBtn->setEnabled(!isServer && !m_clientConnected);
    m_disconnectBtn->setEnabled(!isServer && m_clientConnected);
    m_controlModeBtn->setEnabled(!isServer && m_clientConnected);

    // 服务端模式不需要本地显示远程桌面，仅客户端模式显示右侧画面区域。
    m_remoteDesktopBox->setVisible(!isServer);
    if (m_leftPanelToggleBtn) {
      m_leftPanelToggleBtn->setVisible(!isServer);
    }
    if (isServer) {
      m_leftPanelCollapsed = false;
      if (m_leftPanel) {
        m_leftPanel->setVisible(true);
      }
      if (m_leftPanelToggleBtn) {
        m_leftPanelToggleBtn->setText(QStringLiteral("◀"));
      }
      m_videoWidget->clearFrame();
      m_softwareWidget->clearFrame();
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
    if (!isServer && m_leftPanelToggleBtn) {
      m_leftPanelToggleBtn->setText(m_leftPanelCollapsed ? QStringLiteral("▶") : QStringLiteral("◀"));
      m_leftPanelToggleBtn->setToolTip(m_leftPanelCollapsed ? QStringLiteral("展开左侧控制栏")
                                                            : QStringLiteral("收起左侧控制栏"));
    }
  };
  connect(m_modeServer, &QRadioButton::toggled, this, [toggleMode]() { toggleMode(); });
  toggleMode();
}

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

void MainWindow::appendLog(const QString& text) {
  const QString ts = QDateTime::currentDateTime().toString("HH:mm:ss");
  m_logEdit->append(QStringLiteral("[%1] %2").arg(ts, text));
}

void MainWindow::updateWindowSizeByMode(bool isServerMode) {
  if (isServerMode) {
    // 切到服务端时固定窄宽，避免模式来回切换后残留空白区域。
    if (width() >= 900) {
      m_clientModeSize = size();
    }
    const int serverWidth = 430;
    setMinimumWidth(serverWidth);
    setMaximumWidth(serverWidth);
    setMinimumHeight(760);
    resize(serverWidth, qMax(760, height()));
  } else {
    // 切回客户端时恢复为宽窗口，确保远程桌面区域可用。
    const QSize fallbackClientSize(1300, 820);
    const QSize target = (m_clientModeSize.width() >= 900) ? m_clientModeSize : fallbackClientSize;
    // 明确解除服务端的固定尺寸限制，恢复最大化能力。
    setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    setMinimumSize(980, 760);
    resize(target);
  }
}
