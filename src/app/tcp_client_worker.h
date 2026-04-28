#pragma once

#include "protocol_qt.h"

#include <QObject>
#include <QTcpSocket>

class TcpClientWorker : public QObject {
  Q_OBJECT
public:
  explicit TcpClientWorker(QObject* parent = nullptr);

signals:
  void connected(bool ok, const QString& reason);
  void logMessage(const QString& text);
  void fullFrameArrived(const QImage& image);
  void patchArrived(const QVector<rdqt::PatchBlock>& patches);
  void socketClosed();
  void pingRttUpdated(int ms);

public slots:
  void connectToHost(const QString& ip, quint16 port, const QString& verifyCode, rdqt::QualityPreset preset);
  void disconnectHost();
  void sendInputEvent(const rdqt::RemoteInputEvent& event);
  void sendPing(qint64 clientTsMs);

private slots:
  void onReadyRead();
  void onDisconnected();

private:
  bool tryParseMessage(QByteArray& body, rdqt::MessageType& typeOut);
  void processMessage(rdqt::MessageType type, const QByteArray& body);
  void ensureSocketCreated();

private:
  QTcpSocket* m_socket = nullptr;
  QByteArray m_readBuffer;
  bool m_waitingAck = false;
};
