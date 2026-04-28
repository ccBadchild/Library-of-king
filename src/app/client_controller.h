#pragma once

#include "protocol_qt.h"
#include "tcp_client_worker.h"

#include <QObject>
#include <QTimer>
#include <QThread>

class ClientController : public QObject {
  Q_OBJECT
public:
  explicit ClientController(QObject* parent = nullptr);
  ~ClientController() override;

signals:
  void appendLog(const QString& text);
  void frameUpdated(const QImage& image);
  void stateChanged(bool connected, const QString& reason);
  void qosUpdated(int fps, int kbps, int reconnectCount, int latencyMs, const QString& resolution);

public slots:
  void startConnect(const QString& ip, quint16 port, const QString& code, rdqt::QualityPreset preset);
  void stopConnect();
  void sendRemoteInput(const rdqt::RemoteInputEvent& event);

private:
  void applyPatches(const QVector<rdqt::PatchBlock>& patches);

private:
  QThread m_networkThread;
  TcpClientWorker* m_worker = nullptr;
  QImage m_currentFrame;
  QTimer m_reconnectTimer;
  QString m_lastIp;
  QString m_lastCode;
  quint16 m_lastPort = 0;
  rdqt::QualityPreset m_lastPreset = rdqt::QualityPreset::Medium;
  bool m_autoReconnect = true;
  bool m_userInitiatedDisconnect = false;
  int m_reconnectCount = 0;
  int m_recvFramesWindow = 0;
  qint64 m_recvBytesWindow = 0;
  QTimer m_qosTimer;
  QTimer m_pingTimer;
  int m_lastLatencyMs = -1;
  QString m_lastResolution = "-";
};
