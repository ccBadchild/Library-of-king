#pragma once

#include "protocol_qt.h"

#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>

class TcpFileServerWorker : public QObject {
  Q_OBJECT
public:
  explicit TcpFileServerWorker(QObject* parent = nullptr);

signals:
  void logMessage(const QString& text);

public slots:
  void startListen(quint16 port, const QString& verifyCode);
  void stopListen();

private slots:
  void onNewConnection();
  void onSocketReadyRead();
  void onSocketDisconnected();

private:
  bool tryParseMessage(QByteArray& body, rdqt::MessageType& typeOut);
  void ensureServerCreated();
  void resetClient();
  QVector<rdqt::FileEntry> listRoots() const;
  QVector<rdqt::FileEntry> listDirectory(const QString& path) const;
  QByteArray readFileData(const QString& path, QString* error) const;
  bool writeFileData(const QString& path, const QByteArray& data, QString* error) const;
  QByteArray readFileChunk(const QString& path, qint64 offset, qint32 maxBytes, qint64* totalSize, QString* error) const;
  bool writeFileChunk(const QString& path, qint64 totalSize, qint64 offset, const QByteArray& data, QString* error) const;
  bool deletePath(const QString& path, bool recursive, QString* error) const;

private:
  QTcpServer* m_server = nullptr;
  QTcpSocket* m_client = nullptr;
  QByteArray m_readBuffer;
  QString m_verifyCode;
  bool m_clientVerified = false;
};
