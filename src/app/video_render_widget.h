#pragma once

/**
 * @file video_render_widget.h
 * @brief QOpenGLWidget 路径渲染远端画面：纹理上传 + 全屏三角形绘制；并把鼠标键盘映射为 RemoteInputEvent。
 */

#include "protocol_qt.h"

#include <QImage>
#include <QMutex>
#include <QOpenGLBuffer>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLWidget>
#include <QElapsedTimer>
#include <QSize>

/**
 * @class VideoRenderWidget
 * @brief setFrameImage 可能在解码线程调用：内部 pendingFrame + mutex，paintGL 中上传纹理。
 *
 * 鼠标坐标转换为「画面矩形内相对坐标」再映射到 0–65535（与远端桌面注入约定一致）。
 */
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
  /** 鼠标移动事件节流（毫秒）。 */
  qint64 m_lastMouseMoveSendMs = 0;
};
