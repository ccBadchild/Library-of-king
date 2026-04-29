/**
 * @file title_bar.cpp
 * @brief TitleBar 构造函数与拖拽逻辑（最大化状态下拖拽会先触发还原）。
 */

#include "title_bar.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>

TitleBar::TitleBar(QWidget* parent) : QWidget(parent) {
  setFixedHeight(38);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

  m_titleLabel = new QLabel(QStringLiteral("远程桌面控制中心"), this);
  m_titleLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

  m_minBtn = new QPushButton(QStringLiteral("—"), this);
  m_maxBtn = new QPushButton(QStringLiteral("□"), this);
  m_closeBtn = new QPushButton(QStringLiteral("×"), this);

  m_minBtn->setObjectName(QStringLiteral("titleMinBtn"));
  m_maxBtn->setObjectName(QStringLiteral("titleMaxBtn"));
  m_closeBtn->setObjectName(QStringLiteral("titleCloseBtn"));

  auto applyBtnStyle = [](QPushButton* b) {
    b->setFixedSize(44, 30);
    b->setFocusPolicy(Qt::NoFocus);
  };
  applyBtnStyle(m_minBtn);
  applyBtnStyle(m_maxBtn);
  applyBtnStyle(m_closeBtn);

  auto* layout = new QHBoxLayout(this);
  layout->setContentsMargins(10, 4, 8, 4);
  layout->setSpacing(6);
  layout->addWidget(m_titleLabel, 1);
  layout->addWidget(m_minBtn);
  layout->addWidget(m_maxBtn);
  layout->addWidget(m_closeBtn);

  connect(m_minBtn, &QPushButton::clicked, this, &TitleBar::minimizeRequested);
  connect(m_maxBtn, &QPushButton::clicked, this, &TitleBar::maximizeRestoreRequested);
  connect(m_closeBtn, &QPushButton::clicked, this, &TitleBar::closeRequested);

  updateMaxButtonText();
}

void TitleBar::setTitleText(const QString& text) {
  m_titleLabel->setText(text);
}

void TitleBar::setMaximized(bool maximized) {
  if (m_maximized == maximized) {
    return;
  }
  m_maximized = maximized;
  updateMaxButtonText();
}

void TitleBar::setMaximizeEnabled(bool enabled) {
  m_maxEnabled = enabled;
  if (m_maxBtn) {
    m_maxBtn->setEnabled(enabled);
  }
}

void TitleBar::mousePressEvent(QMouseEvent* event) {
  if (event->button() != Qt::LeftButton) {
    QWidget::mousePressEvent(event);
    return;
  }
  m_pressed = true;
  m_pressPos = event->globalPos();
  if (window()) {
    m_pressWindowPos = window()->frameGeometry().topLeft();
  }
  event->accept();
}

void TitleBar::mouseMoveEvent(QMouseEvent* event) {
  if (!m_pressed || !window() || (event->buttons() & Qt::LeftButton) == 0) {
    QWidget::mouseMoveEvent(event);
    return;
  }

  if (window()->isMaximized()) {
    // 最大化状态下拖拽：先还原再移动，体验更接近系统标题栏。
    if (m_maxEnabled) {
      emit maximizeRestoreRequested();
    } else {
      return;
    }
    m_pressWindowPos = window()->frameGeometry().topLeft();
    m_pressPos = event->globalPos();
  }

  const QPoint delta = event->globalPos() - m_pressPos;
  window()->move(m_pressWindowPos + delta);
  event->accept();
}

void TitleBar::mouseReleaseEvent(QMouseEvent* event) {
  m_pressed = false;
  QWidget::mouseReleaseEvent(event);
}

void TitleBar::mouseDoubleClickEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton) {
    if (m_maxEnabled) {
      emit maximizeRestoreRequested();
      event->accept();
      return;
    }
  }
  QWidget::mouseDoubleClickEvent(event);
}

void TitleBar::updateMaxButtonText() {
  if (!m_maxBtn) {
    return;
  }
  m_maxBtn->setText(m_maximized ? QStringLiteral("❐") : QStringLiteral("□"));
}

