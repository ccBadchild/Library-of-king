#pragma once

#include <QWidget>

class QLabel;
class QPushButton;

class TitleBar : public QWidget {
  Q_OBJECT
public:
  explicit TitleBar(QWidget* parent = nullptr);

  void setTitleText(const QString& text);
  void setMaximized(bool maximized);
  void setMaximizeEnabled(bool enabled);

signals:
  void minimizeRequested();
  void maximizeRestoreRequested();
  void closeRequested();

protected:
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
  void updateMaxButtonText();

private:
  QLabel* m_titleLabel = nullptr;
  QPushButton* m_minBtn = nullptr;
  QPushButton* m_maxBtn = nullptr;
  QPushButton* m_closeBtn = nullptr;

  bool m_pressed = false;
  QPoint m_pressPos;
  QPoint m_pressWindowPos;
  bool m_maximized = false;
  bool m_maxEnabled = true;
};

