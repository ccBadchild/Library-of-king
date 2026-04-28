#include "video_render_widget.h"

#include <QOpenGLFunctions>
#include <QSurfaceFormat>
#include <QKeyEvent>
#include <QMouseEvent>
#include <cstddef>

namespace {

struct VertexData {
  float px;
  float py;
  float tx;
  float ty;
};

constexpr VertexData kQuadVertices[] = {
    {-1.0f, -1.0f, 0.0f, 1.0f},
    {1.0f, -1.0f, 1.0f, 1.0f},
    {-1.0f, 1.0f, 0.0f, 0.0f},
    {1.0f, 1.0f, 1.0f, 0.0f},
};

} // namespace

VideoRenderWidget::VideoRenderWidget(QWidget* parent) : QOpenGLWidget(parent) {
  QSurfaceFormat fmt = format();
  fmt.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
  fmt.setSwapInterval(1); // 尽量使用 VSync，减少高频刷新的视觉抖动。
  setFormat(fmt);

  setAutoFillBackground(false);
  setAttribute(Qt::WA_OpaquePaintEvent, true);
  setAttribute(Qt::WA_NoSystemBackground, true);
  setUpdateBehavior(QOpenGLWidget::PartialUpdate);
  setCursor(Qt::ArrowCursor);
  setMouseTracking(true);
  setFocusPolicy(Qt::StrongFocus);
  m_inputTimer.start();
}

VideoRenderWidget::~VideoRenderWidget() {
  makeCurrent();
  if (m_textureId != 0) {
    context()->functions()->glDeleteTextures(1, &m_textureId);
    m_textureId = 0;
  }
  m_vbo.destroy();
  m_vao.destroy();
  doneCurrent();
}

void VideoRenderWidget::setFrameImage(const QImage& image) {
  if (image.isNull()) {
    return;
  }
  QMutexLocker locker(&m_frameMutex);
  m_pendingFrame = image.convertToFormat(QImage::Format_RGBA8888);
  m_hasPendingUpload = true;
  update();
}

void VideoRenderWidget::clearFrame() {
  QMutexLocker locker(&m_frameMutex);
  m_frame = QImage();
  m_pendingFrame = QImage();
  m_hasPendingUpload = false;
  update();
}

void VideoRenderWidget::initializeGL() {
  auto* f = context()->functions();
  f->glDisable(GL_DEPTH_TEST);
  f->glClearColor(0.07f, 0.07f, 0.07f, 1.0f);

  m_program.addShaderFromSourceCode(QOpenGLShader::Vertex,
                                    "attribute vec2 aPos;\n"
                                    "attribute vec2 aUv;\n"
                                    "varying vec2 vUv;\n"
                                    "void main() {\n"
                                    "  vUv = aUv;\n"
                                    "  gl_Position = vec4(aPos, 0.0, 1.0);\n"
                                    "}\n");
  m_program.addShaderFromSourceCode(QOpenGLShader::Fragment,
                                    "uniform sampler2D uTex;\n"
                                    "varying vec2 vUv;\n"
                                    "void main() {\n"
                                    "  gl_FragColor = texture2D(uTex, vUv);\n"
                                    "}\n");
  m_program.link();

  m_vao.create();
  m_vao.bind();

  m_vbo.create();
  m_vbo.bind();
  m_vbo.allocate(kQuadVertices, static_cast<int>(sizeof(kQuadVertices)));

  const int posLoc = m_program.attributeLocation("aPos");
  const int uvLoc = m_program.attributeLocation("aUv");
  m_program.enableAttributeArray(posLoc);
  m_program.enableAttributeArray(uvLoc);
  m_program.setAttributeBuffer(posLoc, GL_FLOAT, offsetof(VertexData, px), 2, sizeof(VertexData));
  m_program.setAttributeBuffer(uvLoc, GL_FLOAT, offsetof(VertexData, tx), 2, sizeof(VertexData));

  m_vbo.release();
  m_vao.release();

  f->glGenTextures(1, &m_textureId);
  f->glBindTexture(GL_TEXTURE_2D, m_textureId);
  f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  f->glBindTexture(GL_TEXTURE_2D, 0);
}

void VideoRenderWidget::paintGL() {
  auto* f = context()->functions();
  f->glClear(GL_COLOR_BUFFER_BIT);

  QImage pending;
  bool hasUpload = false;
  {
    QMutexLocker locker(&m_frameMutex);
    if (m_hasPendingUpload) {
      pending = m_pendingFrame;
      m_hasPendingUpload = false;
      hasUpload = true;
    }
  }
  if (hasUpload && !pending.isNull()) {
    updateTexture(pending);
    {
      QMutexLocker locker(&m_frameMutex);
      m_frame = pending;
    }
  }

  QImage frameCopy;
  {
    QMutexLocker locker(&m_frameMutex);
    frameCopy = m_frame;
  }
  if (frameCopy.isNull() || m_textureId == 0) {
    m_drawRect = QRect();
    return;
  }

  const QSize drawSize = frameCopy.size().scaled(size(), Qt::KeepAspectRatio);
  const int vpX = (width() - drawSize.width()) / 2;
  const int vpY = (height() - drawSize.height()) / 2;
  m_drawRect = QRect(vpX, vpY, drawSize.width(), drawSize.height());
  f->glViewport(vpX, vpY, drawSize.width(), drawSize.height());

  m_program.bind();
  f->glActiveTexture(GL_TEXTURE0);
  f->glBindTexture(GL_TEXTURE_2D, m_textureId);
  m_program.setUniformValue("uTex", 0);
  m_vao.bind();
  f->glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  m_vao.release();
  f->glBindTexture(GL_TEXTURE_2D, 0);
  m_program.release();

  f->glViewport(0, 0, width(), height());
}

void VideoRenderWidget::resizeGL(int w, int h) {
  Q_UNUSED(w);
  Q_UNUSED(h);
}

void VideoRenderWidget::updateTexture(const QImage& frame) {
  if (frame.isNull() || m_textureId == 0) {
    return;
  }
  auto* f = context()->functions();
  f->glBindTexture(GL_TEXTURE_2D, m_textureId);

  if (m_textureSize != frame.size()) {
    f->glTexImage2D(GL_TEXTURE_2D,
                    0,
                    GL_RGBA,
                    frame.width(),
                    frame.height(),
                    0,
                    GL_RGBA,
                    GL_UNSIGNED_BYTE,
                    frame.constBits());
    m_textureSize = frame.size();
  } else {
    f->glTexSubImage2D(GL_TEXTURE_2D,
                       0,
                       0,
                       0,
                       frame.width(),
                       frame.height(),
                       GL_RGBA,
                       GL_UNSIGNED_BYTE,
                       frame.constBits());
  }
  f->glBindTexture(GL_TEXTURE_2D, 0);
}

void VideoRenderWidget::mouseMoveEvent(QMouseEvent* event) {
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

void VideoRenderWidget::mousePressEvent(QMouseEvent* event) {
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

void VideoRenderWidget::mouseReleaseEvent(QMouseEvent* event) {
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

void VideoRenderWidget::wheelEvent(QWheelEvent* event) {
  rdqt::RemoteInputEvent e;
  e.type = rdqt::InputEventType::MouseWheel;
  e.delta = event->angleDelta().y();
  e.modifiers = static_cast<qint32>(event->modifiers());
  emit inputEventGenerated(e);
}

void VideoRenderWidget::keyPressEvent(QKeyEvent* event) {
  if (event->isAutoRepeat()) {
    return;
  }
  rdqt::RemoteInputEvent e;
  e.type = rdqt::InputEventType::KeyDown;
  e.key = event->nativeVirtualKey();
  e.modifiers = static_cast<qint32>(event->modifiers());
  emit inputEventGenerated(e);
}

void VideoRenderWidget::keyReleaseEvent(QKeyEvent* event) {
  if (event->isAutoRepeat()) {
    return;
  }
  rdqt::RemoteInputEvent e;
  e.type = rdqt::InputEventType::KeyUp;
  e.key = event->nativeVirtualKey();
  e.modifiers = static_cast<qint32>(event->modifiers());
  emit inputEventGenerated(e);
}

