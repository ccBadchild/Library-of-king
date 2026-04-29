#include "mainwindow.h"
#include "app_logger.h"
#include "title_bar.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDateTime>
#include <QIcon>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHostAddress>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QNetworkInterface>
#include <QStyle>
#include <QSystemTrayIcon>
#include <QVBoxLayout>

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
  setupUi();
  bindEvents();
  // bindEvents 末尾 toggleMode() 已将窗口收窄；此处记录客户端模式下的控制面板尺寸。
  m_clientModeSize = size();
}

MainWindow::~MainWindow() {
  // 远程桌面窗口不使用 MainWindow 作 parent，才能在 Windows 任务栏单独显示按钮。
  delete m_clientVideoWindow;
  m_clientVideoWindow = nullptr;
}

void MainWindow::setupUi() {
  setWindowTitle(QStringLiteral("远程桌面控制中心"));
  setWindowIcon(QIcon(QStringLiteral(":/icons/tray_icon.png")));
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

  m_clientVideoWindow = new ClientVideoWindow(nullptr);
  m_clientVideoWindow->hide();

  root->addWidget(m_leftPanel);
}

void MainWindow::bindEvents() {
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
  connect(m_titleBar, &TitleBar::closeRequested, this, &MainWindow::close);

  setupTrayIcon();

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

  connect(m_startServerBtn, &QPushButton::clicked, this, [this]() {
    const quint16 port = m_serverPortEdit->text().toUShort();
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

  connect(&m_clientController, &ClientController::frameUpdated, this, [this](const QImage& image) {
    if (image.isNull()) {
      return;
    }
    // 仅更新当前可见的渲染页，避免 OpenGL / 软件 双路径每帧各渲染一次。
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
  auto inputHandler = [this](const rdqt::RemoteInputEvent& event) {
    if (!m_modeClient->isChecked() || !m_clientConnected || !m_remoteControlEnabled) {
      return;
    }
    m_clientController.sendRemoteInput(event);
  };
  connect(m_clientVideoWindow->videoWidget(), &VideoRenderWidget::inputEventGenerated, this, inputHandler);
  connect(m_clientVideoWindow->softwareWidget(), &SoftwareRenderWidget::inputEventGenerated, this, inputHandler);

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

void MainWindow::setupTrayIcon() {
  QIcon icon(QStringLiteral(":/icons/tray_icon.png"));
  if (icon.isNull()) {
    icon = style()->standardIcon(QStyle::SP_ComputerIcon);
  }

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
  Q_UNUSED(event);
  QApplication::quit();
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

void MainWindow::updateControlAvailability() {
  const bool isServer = m_modeServer->isChecked();

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
  m_controlModeBtn->setEnabled(!isServer && m_clientConnected);
}

void MainWindow::appendLog(const QString& text) {
  const QString ts = QDateTime::currentDateTime().toString("HH:mm:ss");
  m_logEdit->append(QStringLiteral("[%1] %2").arg(ts, text));
}

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
