#include "software_render_widget.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>

SoftwareRenderWidget::SoftwareRenderWidget(QWidget* parent) : QWidget(parent) {
  setAttribute(Qt::WA_OpaquePaintEvent, true);
  setMouseTracking(true);
  setFocusPolicy(Qt::StrongFocus);
  setCursor(Qt::ArrowCursor);
  m_inputTimer.start();
}

void SoftwareRenderWidget::setFrameImage(const QImage& image) {
  if (image.isNull()) {
    return;
  }
  QMutexLocker locker(&m_frameMutex);
  m_frame = image;
  update();
}

void SoftwareRenderWidget::clearFrame() {
  QMutexLocker locker(&m_frameMutex);
  m_frame = QImage();
  update();
}

void SoftwareRenderWidget::paintEvent(QPaintEvent* event) {
  Q_UNUSED(event);
  QPainter painter(this);
  painter.fillRect(rect(), QColor(17, 17, 17));

  QImage frameCopy;
  {
    QMutexLocker locker(&m_frameMutex);
    frameCopy = m_frame;
  }
  if (frameCopy.isNull()) {
    painter.setPen(QColor(220, 220, 220));
    painter.drawText(rect(), Qt::AlignCenter, QStringLiteral("等待画面..."));
    m_drawRect = QRect();
    return;
  }

  const QSize drawSize = frameCopy.size().scaled(size(), Qt::KeepAspectRatio);
  const int x = (width() - drawSize.width()) / 2;
  const int y = (height() - drawSize.height()) / 2;
  m_drawRect = QRect(x, y, drawSize.width(), drawSize.height());
  painter.drawImage(m_drawRect, frameCopy);
}

void SoftwareRenderWidget::mouseMoveEvent(QMouseEvent* event) {
  if (m_drawRect.isEmpty() || !m_drawRect.contains(event->pos())) {
    return;
  }
  const qint64 nowMs = m_inputTimer.elapsed();
  if (nowMs - m_lastMouseMoveSendMs < 8) {
    return;
  }
  m_lastMouseMoveSendMs = nowMs;

  const int relX = event->pos().x() - m_drawRect.left();
  const int relY = event->pos().y() - m_drawRect.top();
  rdqt::RemoteInputEvent e;
  e.type = rdqt::InputEventType::MouseMove;
  e.x = qBound(0, relX * 65535 / qMax(1, m_drawRect.width()), 65535);
  e.y = qBound(0, relY * 65535 / qMax(1, m_drawRect.height()), 65535);
  e.buttons = static_cast<qint32>(event->buttons());
  e.modifiers = static_cast<qint32>(event->modifiers());
  emit inputEventGenerated(e);
}

void SoftwareRenderWidget::mousePressEvent(QMouseEvent* event) {
  setFocus();
  if (m_drawRect.isEmpty() || !m_drawRect.contains(event->pos())) {
    return;
  }
  const int relX = event->pos().x() - m_drawRect.left();
  const int relY = event->pos().y() - m_drawRect.top();
  rdqt::RemoteInputEvent e;
  e.type = rdqt::InputEventType::MouseDown;
  e.x = qBound(0, relX * 65535 / qMax(1, m_drawRect.width()), 65535);
  e.y = qBound(0, relY * 65535 / qMax(1, m_drawRect.height()), 65535);
  e.buttons = static_cast<qint32>(event->button());
  e.modifiers = static_cast<qint32>(event->modifiers());
  emit inputEventGenerated(e);
}

void SoftwareRenderWidget::mouseReleaseEvent(QMouseEvent* event) {
  if (m_drawRect.isEmpty() || !m_drawRect.contains(event->pos())) {
    return;
  }
  const int relX = event->pos().x() - m_drawRect.left();
  const int relY = event->pos().y() - m_drawRect.top();
  rdqt::RemoteInputEvent e;
  e.type = rdqt::InputEventType::MouseUp;
  e.x = qBound(0, relX * 65535 / qMax(1, m_drawRect.width()), 65535);
  e.y = qBound(0, relY * 65535 / qMax(1, m_drawRect.height()), 65535);
  e.buttons = static_cast<qint32>(event->button());
  e.modifiers = static_cast<qint32>(event->modifiers());
  emit inputEventGenerated(e);
}

void SoftwareRenderWidget::wheelEvent(QWheelEvent* event) {
  rdqt::RemoteInputEvent e;
  e.type = rdqt::InputEventType::MouseWheel;
  e.delta = event->angleDelta().y();
  e.modifiers = static_cast<qint32>(event->modifiers());
  emit inputEventGenerated(e);
}

void SoftwareRenderWidget::keyPressEvent(QKeyEvent* event) {
  if (event->isAutoRepeat()) {
    return;
  }
  rdqt::RemoteInputEvent e;
  e.type = rdqt::InputEventType::KeyDown;
  e.key = event->nativeVirtualKey();
  e.modifiers = static_cast<qint32>(event->modifiers());
  emit inputEventGenerated(e);
}

void SoftwareRenderWidget::keyReleaseEvent(QKeyEvent* event) {
  if (event->isAutoRepeat()) {
    return;
  }
  rdqt::RemoteInputEvent e;
  e.type = rdqt::InputEventType::KeyUp;
  e.key = event->nativeVirtualKey();
  e.modifiers = static_cast<qint32>(event->modifiers());
  emit inputEventGenerated(e);
}
