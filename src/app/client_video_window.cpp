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

  bodyLayout->addWidget(m_renderStack, 1);
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
    m_titleBar->setMaximized(isMaximized());
  }
}
