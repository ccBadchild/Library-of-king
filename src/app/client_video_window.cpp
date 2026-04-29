/**
 * @file client_video_window.cpp
 * @brief ClientVideoWindow 布局与标题栏行为、关闭校验、窗口状态同步。
 */

#include "client_video_window.h"
#include "software_render_widget.h"
#include "title_bar.h"
#include "video_render_widget.h"

#include <QCloseEvent>
#include <QEvent>
#include <QGraphicsOpacityEffect>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

ClientVideoWindow::ClientVideoWindow(QWidget* parent)
    : QWidget(parent, Qt::Window | Qt::FramelessWindowHint) {
  setWindowTitle(QStringLiteral("远程桌面"));
  resize(1280, 720);
  setMinimumSize(640, 480);

  // 外层边距与主窗 centralWidget 一致（8px），便于共用 dark.qss 观感。
  auto* outer = new QVBoxLayout(this);
  outer->setContentsMargins(8, 8, 8, 8);
  outer->setSpacing(8);

  m_titleBar = new TitleBar(this);
  m_titleBar->setTitleText(QStringLiteral("远程桌面"));
  m_titleBar->setMaximizeEnabled(true);
  outer->addWidget(m_titleBar);

  auto* body = new QWidget(this);
  auto* bodyLayout = new QVBoxLayout(body);
  bodyLayout->setContentsMargins(4, 4, 4, 4);
  bodyLayout->setSpacing(0);

  m_renderStack = new QStackedWidget(body);
  m_videoWidget = new VideoRenderWidget(body);
  m_videoWidget->setMinimumSize(640, 480);
  m_softwareWidget = new SoftwareRenderWidget(body);
  m_softwareWidget->setMinimumSize(640, 480);
  m_renderStack->addWidget(m_videoWidget);
  m_renderStack->addWidget(m_softwareWidget);
  m_renderStack->setCurrentWidget(m_videoWidget);

  // 标题栏下方悬浮工具栏：当前提供全屏切换入口（后续可继续扩展按钮）。
  m_floatToolbar = new QWidget(body);
  m_floatToolbar->setObjectName(QStringLiteral("videoFloatToolbar"));
  auto* toolbarLayout = new QHBoxLayout(m_floatToolbar);
  toolbarLayout->setContentsMargins(10, 6, 10, 6);
  toolbarLayout->setSpacing(8);
  m_toolbarToggleBtn = new QPushButton(m_floatToolbar);
  m_toolbarToggleBtn->setObjectName(QStringLiteral("videoToolbarToggleBtn"));
  m_toolbarToggleBtn->setCursor(Qt::PointingHandCursor);
  m_toolbarToggleBtn->setFixedSize(30, 28);
  m_transferBtn = new QPushButton(QStringLiteral("远程传输"), m_floatToolbar);
  m_transferBtn->setObjectName(QStringLiteral("videoTransferBtn"));
  m_transferBtn->setCursor(Qt::PointingHandCursor);
  m_fullScreenBtn = new QPushButton(m_floatToolbar);
  m_fullScreenBtn->setObjectName(QStringLiteral("videoFullScreenBtn"));
  m_fullScreenBtn->setCursor(Qt::PointingHandCursor);
  toolbarLayout->addWidget(m_toolbarToggleBtn);
  toolbarLayout->addWidget(m_transferBtn);
  toolbarLayout->addWidget(m_fullScreenBtn);
  m_floatToolbar->setAttribute(Qt::WA_StyledBackground, true);
  m_floatToolbar->setFixedHeight(42);
  m_floatToolbar->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
  m_floatToolbar->setMouseTracking(true);
  m_floatToolbar->installEventFilter(this);
  m_toolbarToggleBtn->installEventFilter(this);
  m_transferBtn->installEventFilter(this);
  m_fullScreenBtn->installEventFilter(this);
  m_renderStack->installEventFilter(this);
  m_videoWidget->installEventFilter(this);
  m_softwareWidget->installEventFilter(this);
  m_autoCollapseTimer = new QTimer(this);
  m_autoCollapseTimer->setSingleShot(true);
  m_toolbarOpacityEffect = new QGraphicsOpacityEffect(m_floatToolbar);
  m_toolbarOpacityEffect->setOpacity(0.0);
  m_floatToolbar->setGraphicsEffect(m_toolbarOpacityEffect);
  m_toolbarFadeAnimation = new QPropertyAnimation(m_toolbarOpacityEffect, "opacity", this);
  m_toolbarFadeAnimation->setDuration(180);
  connect(m_toolbarFadeAnimation, &QPropertyAnimation::finished, this, [this]() {
    if (!m_toolbarShown && m_toolbarOpacityEffect && m_toolbarOpacityEffect->opacity() <= 0.01) {
      m_floatToolbar->hide();
    }
  });
  updateFullScreenButtonText();
  setToolbarExpanded(false);
  m_floatToolbar->hide();

  // 用网格重叠：渲染区铺底，工具栏叠在顶部居中，实现“悬浮”效果。
  auto* overlayLayout = new QGridLayout();
  overlayLayout->setContentsMargins(0, 0, 0, 0);
  overlayLayout->setSpacing(0);
  overlayLayout->addWidget(m_renderStack, 0, 0);
  overlayLayout->addWidget(m_floatToolbar, 0, 0, Qt::AlignHCenter | Qt::AlignTop);
  bodyLayout->addLayout(overlayLayout, 1);
  outer->addWidget(body, 1);

  // 此处最小化仍为系统最小化（远程窗一般不隐藏到托盘，由任务栏恢复）。
  connect(m_titleBar, &TitleBar::minimizeRequested, this, &ClientVideoWindow::showMinimized);
  connect(m_titleBar, &TitleBar::maximizeRestoreRequested, this, [this]() {
    if (isMaximized()) {
      showNormal();
    } else {
      showMaximized();
    }
    if (m_titleBar) {
      m_titleBar->setMaximized(isMaximized());
    }
  });
  connect(m_titleBar, &TitleBar::closeRequested, this, &ClientVideoWindow::close);
  connect(m_autoCollapseTimer, &QTimer::timeout, this, [this]() {
    setToolbarExpanded(false);
  });
  connect(m_toolbarToggleBtn, &QPushButton::clicked, this, [this]() {
    setToolbarExpanded(!m_toolbarExpanded);
  });
  connect(m_transferBtn, &QPushButton::clicked, this, [this]() {
    emit transferRequested();
    restartAutoCollapseTimer();
  });
  connect(m_fullScreenBtn, &QPushButton::clicked, this, [this]() {
    if (isFullScreen()) {
      showNormal();
    } else {
      showFullScreen();
    }
    if (m_titleBar) {
      m_titleBar->setVisible(!isFullScreen());
    }
    updateFullScreenButtonText();
    restartAutoCollapseTimer();
  });

  setMouseTracking(true);
  body->setMouseTracking(true);
  m_renderStack->setMouseTracking(true);
  m_videoWidget->setMouseTracking(true);
  m_softwareWidget->setMouseTracking(true);
}

void ClientVideoWindow::setCloseVerifier(std::function<bool()> verifier) {
  m_closeVerifier = std::move(verifier);
}

void ClientVideoWindow::closeEvent(QCloseEvent* event) {
  if (m_closeVerifier && !m_closeVerifier()) {
    event->ignore();
    return;
  }
  event->accept();
}

void ClientVideoWindow::changeEvent(QEvent* event) {
  QWidget::changeEvent(event);
  if (event->type() == QEvent::WindowStateChange && m_titleBar) {
    m_titleBar->setVisible(!isFullScreen());
    m_titleBar->setMaximized(isMaximized());
    updateFullScreenButtonText();
  }
}

void ClientVideoWindow::keyPressEvent(QKeyEvent* event) {
  if (event->key() == Qt::Key_Escape && isFullScreen()) {
    setWindowState(windowState() & ~Qt::WindowFullScreen);
    showNormal();
    if (m_titleBar) {
      m_titleBar->show();
    }
    updateFullScreenButtonText();
    restartAutoCollapseTimer();
    event->accept();
    return;
  }
  QWidget::keyPressEvent(event);
}

bool ClientVideoWindow::eventFilter(QObject* watched, QEvent* event) {
  if (watched == m_renderStack || watched == m_videoWidget || watched == m_softwareWidget) {
    if (event->type() == QEvent::MouseMove) {
      const auto* mouseEvent = static_cast<QMouseEvent*>(event);
      const QPoint mappedPos = mapFromGlobal(mouseEvent->globalPos());
      updateToolbarVisibilityByPointer(mappedPos);
    } else if (event->type() == QEvent::Leave && !m_toolbarExpanded) {
      setToolbarVisibleAnimated(false);
    }
  }

  if (watched == m_floatToolbar || watched == m_toolbarToggleBtn || watched == m_transferBtn || watched == m_fullScreenBtn) {
    if (event->type() == QEvent::Enter) {
      setToolbarVisibleAnimated(true);
      if (m_toolbarExpanded) {
        restartAutoCollapseTimer();
      }
    } else if (event->type() == QEvent::Leave) {
      if (m_toolbarExpanded) {
        restartAutoCollapseTimer();
      } else {
        setToolbarVisibleAnimated(false);
      }
    } else if (event->type() == QEvent::MouseMove) {
      setToolbarVisibleAnimated(true);
      if (m_toolbarExpanded) {
        restartAutoCollapseTimer();
      }
    }
  }
  return QWidget::eventFilter(watched, event);
}

void ClientVideoWindow::updateFullScreenButtonText() {
  if (!m_fullScreenBtn) {
    return;
  }
  m_fullScreenBtn->setText(isFullScreen() ? QStringLiteral("退出全屏") : QStringLiteral("全屏"));
}

void ClientVideoWindow::setToolbarExpanded(bool expanded) {
  m_toolbarExpanded = expanded;
  if (m_fullScreenBtn) {
    m_fullScreenBtn->setVisible(expanded);
  }
  if (m_transferBtn) {
    m_transferBtn->setVisible(expanded);
  }
  if (m_toolbarToggleBtn) {
    m_toolbarToggleBtn->setText(expanded ? QStringLiteral("˄") : QStringLiteral("˅"));
    m_toolbarToggleBtn->setToolTip(expanded ? QStringLiteral("收起工具栏") : QStringLiteral("展开工具栏"));
  }
  if (m_autoCollapseTimer) {
    if (expanded) {
      restartAutoCollapseTimer();
      setToolbarVisibleAnimated(true);
    } else {
      m_autoCollapseTimer->stop();
      if (!underMouse()) {
        setToolbarVisibleAnimated(false);
      }
    }
  }
  if (m_floatToolbar) {
    m_floatToolbar->adjustSize();
  }
}

void ClientVideoWindow::restartAutoCollapseTimer() {
  if (m_autoCollapseTimer && m_toolbarExpanded) {
    m_autoCollapseTimer->start(10000);
  }
}

bool ClientVideoWindow::isInToolbarHotZone(const QPoint& localPos) const {
  constexpr int kTopHotZoneHeight = 90;
  return rect().contains(localPos) && localPos.y() >= 0 && localPos.y() <= kTopHotZoneHeight;
}

void ClientVideoWindow::updateToolbarVisibilityByPointer(const QPoint& localPos) {
  if (m_toolbarExpanded) {
    setToolbarVisibleAnimated(true);
    restartAutoCollapseTimer();
    return;
  }
  setToolbarVisibleAnimated(isInToolbarHotZone(localPos));
}

void ClientVideoWindow::setToolbarVisibleAnimated(bool visible) {
  if (!m_floatToolbar || !m_toolbarOpacityEffect || !m_toolbarFadeAnimation) {
    return;
  }
  if (m_toolbarShown == visible) {
    return;
  }
  m_toolbarShown = visible;
  m_toolbarFadeAnimation->stop();
  if (visible) {
    m_floatToolbar->show();
    m_toolbarFadeAnimation->setStartValue(m_toolbarOpacityEffect->opacity());
    m_toolbarFadeAnimation->setEndValue(1.0);
    m_toolbarFadeAnimation->start();
  } else {
    m_toolbarFadeAnimation->setStartValue(m_toolbarOpacityEffect->opacity());
    m_toolbarFadeAnimation->setEndValue(0.0);
    m_toolbarFadeAnimation->start();
  }
}
