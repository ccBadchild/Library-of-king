#pragma once

#include "protocol_qt.h"

#include <QImage>
#include <QMutex>
#include <QOpenGLBuffer>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLWidget>
#include <QElapsedTimer>
#include <QSize>

// 使用 QOpenGLWidget 承载视频绘制，减少 QLabel/QPixmap 频繁拷贝带来的开销。
class VideoRenderWidget : public QOpenGLWidget {
  Q_OBJECT
public:
  explicit VideoRenderWidget(QWidget* parent = nullptr);
  ~VideoRenderWidget() override;

signals:
  void inputEventGenerated(const rdqt::RemoteInputEvent& event);

public slots:
  void setFrameImage(const QImage& image);
  void clearFrame();

protected:
  void initializeGL() override;
  void paintGL() override;
  void resizeGL(int w, int h) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;
  void keyReleaseEvent(QKeyEvent* event) override;

private:
  void updateTexture(const QImage& frame);

private:
  QMutex m_frameMutex;
  QImage m_frame;
  QImage m_pendingFrame;

  QOpenGLShaderProgram m_program;
  QOpenGLBuffer m_vbo{QOpenGLBuffer::VertexBuffer};
  QOpenGLVertexArrayObject m_vao;
  GLuint m_textureId = 0;
  QSize m_textureSize;
  bool m_hasPendingUpload = false;
  QRect m_drawRect;
  QElapsedTimer m_inputTimer;
  qint64 m_lastMouseMoveSendMs = 0;
};
