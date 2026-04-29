#pragma once

#include "protocol_qt.h"

#include <QObject>
#include <QTcpSocket>

class TcpFileClientWorker : public QObject {
  Q_OBJECT
public:
  explicit TcpFileClientWorker(QObject* parent = nullptr);

signals:
  void connected(bool ok, const QString& reason);
  void logMessage(const QString& text);
  void rootsListed(const QVector<rdqt::FileEntry>& entries);
  void directoryListed(const QString& path, const QVector<rdqt::FileEntry>& entries);
  void fileReceived(const QString& remotePath, bool ok, const QString& reason, const QByteArray& data);
  void fileWritten(const QString& remotePath, bool ok, const QString& reason);
  void fileChunkReceived(const QString& remotePath,
                         qint64 totalSize,
                         qint64 offset,
                         bool done,
                         bool ok,
                         const QString& reason,
                         const QByteArray& data);
  void fileChunkWritten(const QString& remotePath,
                        qint64 totalSize,
                        qint64 offset,
                        qint32 writtenBytes,
                        bool done,
                        bool ok,
                        const QString& reason);
  void pathDeleted(const QString& remotePath, bool ok, const QString& reason);

public slots:
  void connectToHost(const QString& ip, quint16 port, const QString& verifyCode);
  void disconnectHost();
  void requestRoots();
  void requestDirectory(const QString& path);
  void requestFile(const QString& path);
  void sendFile(const QString& targetPath, const QByteArray& data);
  void requestFileChunk(const QString& path, qint64 offset, qint32 maxBytes);
  void sendFileChunk(const QString& targetPath, qint64 totalSize, qint64 offset, const QByteArray& data);
  void requestDeletePath(const QString& path, bool recursive);

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
