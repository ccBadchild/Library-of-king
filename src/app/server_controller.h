#pragma once

#include "capture_worker.h"
#include "tcp_server_worker.h"

#include <QObject>
#include <QThread>

class ServerController : public QObject {
  Q_OBJECT
public:
  explicit ServerController(QObject* parent = nullptr);
  ~ServerController() override;

  QString verifyCode() const;

signals:
  void appendLog(const QString& text);
  void qosUpdated(int fps, int kbps, int jpegQuality);

public slots:
  void startServer(quint16 port);
  void stopServer();

private:
  QString generateVerifyCode() const;

private:
  QString m_verifyCode;
  QThread m_networkThread;
  QThread m_captureThread;
  TcpServerWorker* m_serverWorker = nullptr;
  CaptureWorker* m_captureWorker = nullptr;
};
