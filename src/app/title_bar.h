#pragma once

/**
 * @file title_bar.h
 * @brief 无边框窗口使用的自定义标题栏：标题文本与最小化/最大化/关闭按钮；支持拖拽移动与双击最大化。
 */

#include <QWidget>

class QLabel;
class QPushButton;

/**
 * @class TitleBar
 * @brief 高度固定的横向条，样式见 dark.qss 中 TitleBar 及 #titleMinBtn 等选择器。
 *
 * 鼠标左键拖拽通过读取顶层 window()->move() 实现；双击标题区域在无最大化按钮可用时不触发还原逻辑。
 */
class TitleBar : public QWidget {
  Q_OBJECT
public:
  explicit TitleBar(QWidget* parent = nullptr);

  void setTitleText(const QString& text);
  /** 同步最大化图标「□」与还原图标「❐」，以及内部 m_maximized 状态。 */
  void setMaximized(bool maximized);
  /** 服务端窄窗口模式下禁用最大化按钮（避免布局不适配）。 */
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
