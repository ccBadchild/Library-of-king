#pragma once

#include "protocol_qt.h"

#include <QImage>
#include <QMutex>
#include <QElapsedTimer>
#include <QWidget>

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
