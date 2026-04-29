#pragma once

/**
 * @file client_video_window.h
 * @brief 客户端专用：独立顶层窗口承载 OpenGL / 软件两套远程画面渲染栈，样式与主窗一致（无边框 + TitleBar）。
 */

#include <QCloseEvent>
#include <QEvent>
#include <QKeyEvent>
#include <QPoint>
#include <QStackedWidget>
#include <QWidget>

#include <functional>

#include "software_render_widget.h"
#include "video_render_widget.h"

class TitleBar;
class QGraphicsOpacityEffect;
class QPropertyAnimation;
class QPushButton;
class QTimer;

/**
 * @class ClientVideoWindow
 * @brief 远程画面独立窗口。默认构造后隐藏，由 MainWindow 在 TCP 连接成功后 show()。
 *
 * - 关闭前可注入 setCloseVerifier：用于「已连接时关窗需确认是否断开」等业务；返回 false 则忽略关闭。
 * - 构造时传入 parent=nullptr：与主控窗分离，Windows 任务栏显示独立按钮。
 */
class ClientVideoWindow : public QWidget {
  Q_OBJECT
public:
  explicit ClientVideoWindow(QWidget* parent = nullptr);

  /**
   * @brief 在 closeEvent 中调用。若返回 false，则 event->ignore()，窗口保持显示。
   * 未设置时默认允许关闭（accept）。
   */
  void setCloseVerifier(std::function<bool()> verifier);

  QStackedWidget* renderStack() const { return m_renderStack; }
  VideoRenderWidget* videoWidget() const { return m_videoWidget; }
  SoftwareRenderWidget* softwareWidget() const { return m_softwareWidget; }

signals:
  void transferRequested();

protected:
  void closeEvent(QCloseEvent* event) override;
  /** 同步标题栏最大化按钮图标与系统最大化/还原状态（含 Win+↑ 等方式触发的状态变化）。 */
  void changeEvent(QEvent* event) override;
  /** 全屏时支持 Esc 快捷退出。 */
  void keyPressEvent(QKeyEvent* event) override;
  /** 监听悬浮栏鼠标进入/离开，用于自动收起计时。 */
  bool eventFilter(QObject* watched, QEvent* event) override;

private:
  /** 根据当前全屏状态刷新按钮文案。 */
  void updateFullScreenButtonText();
  /** 鼠标是否进入远程画面顶部的唤出区域。 */
  bool isInToolbarHotZone(const QPoint& localPos) const;
  /** 切换悬浮栏展开/收起状态，并同步箭头方向。 */
  void setToolbarExpanded(bool expanded);
  /** 展开后启动或续期自动收起计时。 */
  void restartAutoCollapseTimer();
  /** 根据鼠标位置决定是否显示折叠态入口。 */
  void updateToolbarVisibilityByPointer(const QPoint& localPos);
  /** 淡入 / 淡出悬浮工具栏容器。 */
  void setToolbarVisibleAnimated(bool visible);
  /** 可选的关窗前回调；典型用途：询问是否断开远程连接。 */
  std::function<bool()> m_closeVerifier;
  TitleBar* m_titleBar = nullptr;
  QWidget* m_floatToolbar = nullptr;
  QPushButton* m_toolbarToggleBtn = nullptr;
  QPushButton* m_transferBtn = nullptr;
  QPushButton* m_fullScreenBtn = nullptr;
  QTimer* m_autoCollapseTimer = nullptr;
  QGraphicsOpacityEffect* m_toolbarOpacityEffect = nullptr;
  QPropertyAnimation* m_toolbarFadeAnimation = nullptr;
  bool m_toolbarExpanded = false;
  bool m_toolbarShown = false;
  /** 第 0 页为 OpenGL（VideoRenderWidget），第 1 页为软件渲染（SoftwareRenderWidget）。 */
  QStackedWidget* m_renderStack = nullptr;
  VideoRenderWidget* m_videoWidget = nullptr;
  SoftwareRenderWidget* m_softwareWidget = nullptr;
};
