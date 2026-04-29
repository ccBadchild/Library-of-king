#pragma once

#include <QCloseEvent>
#include <QEvent>
#include <QStackedWidget>
#include <QWidget>

#include <functional>

#include "software_render_widget.h"
#include "video_render_widget.h"

class TitleBar;

/** 客户端远程画面独立顶层窗口（无边框 + 与控制界面一致的深色样式），默认隐藏。 */
class ClientVideoWindow : public QWidget {
  Q_OBJECT
public:
  explicit ClientVideoWindow(QWidget* parent = nullptr);

  /** 关闭窗口前调用；返回 false 则取消关闭（忽略关闭事件）。未设置则一律允许关闭。 */
  void setCloseVerifier(std::function<bool()> verifier);

  QStackedWidget* renderStack() const { return m_renderStack; }
  VideoRenderWidget* videoWidget() const { return m_videoWidget; }
  SoftwareRenderWidget* softwareWidget() const { return m_softwareWidget; }

protected:
  void closeEvent(QCloseEvent* event) override;
  void changeEvent(QEvent* event) override;

private:
  std::function<bool()> m_closeVerifier;
  TitleBar* m_titleBar = nullptr;
  QStackedWidget* m_renderStack = nullptr;
  VideoRenderWidget* m_videoWidget = nullptr;
  SoftwareRenderWidget* m_softwareWidget = nullptr;
};
