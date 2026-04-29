#pragma once

#include "software_render_widget.h"
#include "video_render_widget.h"
#include "client_controller.h"
#include "server_controller.h"

#include <QComboBox>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QPushButton>
#include <QRadioButton>
#include <QSize>
#include <QStackedWidget>
#include <QTextEdit>
#include <QWidget>

class TitleBar;

class MainWindow : public QMainWindow {
  Q_OBJECT
public:
  explicit MainWindow(QWidget* parent = nullptr);

private:
  void setupUi();
  void bindEvents();
  void appendLog(const QString& text);
  void updateWindowSizeByMode(bool isServerMode);
  rdqt::QualityPreset currentPreset() const;
  rdqt::VideoCodec currentCodec() const;

private:
  ServerController m_serverController;
  ClientController m_clientController;

  TitleBar* m_titleBar = nullptr;
  QWidget* m_leftPanel = nullptr;
  QWidget* m_leftPanelHost = nullptr;
  QPushButton* m_leftPanelToggleBtn = nullptr;

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

  QGroupBox* m_remoteDesktopBox = nullptr;
  QStackedWidget* m_renderStack = nullptr;
  VideoRenderWidget* m_videoWidget = nullptr;
  SoftwareRenderWidget* m_softwareWidget = nullptr;
  QTextEdit* m_logEdit = nullptr;
  QSize m_clientModeSize;
  bool m_clientConnected = false;
  bool m_remoteControlEnabled = false;
  bool m_isLoopbackTarget = false;
  bool m_leftPanelCollapsed = false;
};
