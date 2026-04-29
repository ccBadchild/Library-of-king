#pragma once

/**
 * @file software_render_widget.h
 * @brief QWidget + QPainter 软件渲染远端画面（无需 OpenGL）；交互逻辑与 VideoRenderWidget 对齐。
 */

#include "protocol_qt.h"

#include <QImage>
#include <QMutex>
#include <QElapsedTimer>
#include <QWidget>

/**
 * @class SoftwareRenderWidget
 * @brief paintEvent 中按比例缩放居中绘制；输入坐标换算方式与 OpenGL 版本一致。
 */
class SoftwareRenderWidget : public QWidget {
  Q_OBJECT
public:
  explicit SoftwareRenderWidget(QWidget* parent = nullptr);

signals:
  void inputEventGenerated(const rdqt::RemoteInputEvent& event);

public slots:
  void setFrameImage(const QImage& image);
  void clearFrame();

protected:
  void paintEvent(QPaintEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;
  void keyReleaseEvent(QKeyEvent* event) override;

private:
  QRect m_drawRect;
  QMutex m_frameMutex;
  QImage m_frame;
  QElapsedTimer m_inputTimer;
  qint64 m_lastMouseMoveSendMs = 0;
};
