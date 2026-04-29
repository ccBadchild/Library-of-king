#pragma once

#include "client_controller.h"
#include "client_video_window.h"
#include "server_controller.h"

#include <QCloseEvent>
#include <QComboBox>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QPushButton>
#include <QRadioButton>
#include <QSize>
#include <QTextEdit>
#include <QWidget>

class TitleBar;
class QSystemTrayIcon;

class MainWindow : public QMainWindow {
  Q_OBJECT
public:
  explicit MainWindow(QWidget* parent = nullptr);
  ~MainWindow() override;

protected:
  void closeEvent(QCloseEvent* event) override;

private:
  void setupUi();
  void bindEvents();
  void appendLog(const QString& text);
  void updateControlAvailability();
  void setupTrayIcon();
  void updateWindowSizeByMode(bool isServerMode);
  rdqt::QualityPreset currentPreset() const;
  rdqt::VideoCodec currentCodec() const;

private:
  ServerController m_serverController;
  ClientController m_clientController;

  TitleBar* m_titleBar = nullptr;
  QWidget* m_leftPanel = nullptr;

  QRadioButton* m_modeServer = nullptr;
  QRadioButton* m_modeClient = nullptr;
  QLineEdit* m_serverPortEdit = nullptr;
  QLabel* m_verifyCodeLabel = nullptr;
  QPushButton* m_startServerBtn = nullptr;
  QPushButton* m_stopServerBtn = nullptr;

  QLineEdit* m_clientIpEdit = nullptr;
  QLineEdit* m_clientPortEdit = nullptr;
  QLineEdit* m_clientCodeEdit = nullptr;
  QComboBox* m_qualityBox = nullptr;
  QComboBox* m_codecBox = nullptr;
  QComboBox* m_renderModeBox = nullptr;
  QPushButton* m_connectBtn = nullptr;
  QPushButton* m_disconnectBtn = nullptr;
  QPushButton* m_controlModeBtn = nullptr;
  QLabel* m_qosServerLabel = nullptr;
  QLabel* m_qosClientLabel = nullptr;

  ClientVideoWindow* m_clientVideoWindow = nullptr;
  QSystemTrayIcon* m_trayIcon = nullptr;
  QTextEdit* m_logEdit = nullptr;
  QSize m_clientModeSize;
  bool m_clientConnected = false;
  /** 用户已启动服务端监听（用于禁用重复启动与运行模式切换）。 */
  bool m_serverRunning = false;
  bool m_remoteControlEnabled = false;
  bool m_isLoopbackTarget = false;
};
