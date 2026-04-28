#pragma once

#include "protocol_qt.h"

#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>

class TcpServerWorker : public QObject {
  Q_OBJECT
public:
  explicit TcpServerWorker(QObject* parent = nullptr);

signals:
  void clientAuthed(rdqt::QualityPreset preset);
  void logMessage(const QString& text);
  void networkBacklogChanged(qint64 bytes);

public slots:
  void startListen(quint16 port, const QString& verifyCode);
  void stopListen();
  void sendPacket(const QByteArray& packet);

private slots:
  void onNewConnection();
  void onSocketReadyRead();
  void onSocketDisconnected();

private:
  bool tryParseMessage(QByteArray& body, rdqt::MessageType& typeOut);
  void resetClient();
  void ensureServerCreated();
  void applyRemoteInput(const QByteArray& body);

private:
  QTcpServer* m_server = nullptr;
  QTcpSocket* m_client = nullptr;
  QByteArray m_readBuffer;
  QString m_verifyCode;
  bool m_clientVerified = false;
  quint64 m_inputRecvCount = 0;
};
