#include "tcp_file_client_worker.h"

#include <QDataStream>

TcpFileClientWorker::TcpFileClientWorker(QObject* parent) : QObject(parent) {
}

void TcpFileClientWorker::connectToHost(const QString& ip, quint16 port, const QString& verifyCode) {
  // 文件通道独立于主控制通道：连接成功后必须先发送 FileHello 做验证码校验，
  // 只有收到 FileHelloAck 才允许后续目录浏览和分片传输请求继续下发。
  m_readBuffer.clear();
  m_waitingAck = true;
  ensureSocketCreated();
  if (!m_socket) {
    emit connected(false, QStringLiteral("文件通道初始化失败"));
    return;
  }
  m_socket->connectToHost(ip, port);
  if (!m_socket->waitForConnected(4000)) {
    emit connected(false, QStringLiteral("文件通道连接失败：%1").arg(m_socket->errorString()));
    return;
  }
  m_socket->write(rdqt::packMessage(rdqt::MessageType::FileHello, rdqt::makeFileHelloPayload(verifyCode)));
}

void TcpFileClientWorker::disconnectHost() {
  if (m_socket) {
    m_socket->disconnectFromHost();
  }
}

void TcpFileClientWorker::requestRoots() {
  // 握手未完成前禁止发送业务消息，避免服务端把非 FileHello 消息当作协议错误直接断开。
  if (!m_socket || m_socket->state() != QAbstractSocket::ConnectedState || m_waitingAck) {
    return;
  }
  m_socket->write(rdqt::packMessage(rdqt::MessageType::FsListRootsReq, {}));
}

void TcpFileClientWorker::requestDirectory(const QString& path) {
  if (!m_socket || m_socket->state() != QAbstractSocket::ConnectedState || m_waitingAck) {
    return;
  }
  m_socket->write(rdqt::packMessage(rdqt::MessageType::FsListDirReq, rdqt::makeListDirPayload(path)));
}

void TcpFileClientWorker::requestFile(const QString& path) {
  if (!m_socket || m_socket->state() != QAbstractSocket::ConnectedState || m_waitingAck) {
    return;
  }
  m_socket->write(rdqt::packMessage(rdqt::MessageType::FsReadFileReq, rdqt::makeReadFilePayload(path)));
}

void TcpFileClientWorker::sendFile(const QString& targetPath, const QByteArray& data) {
  if (!m_socket || m_socket->state() != QAbstractSocket::ConnectedState || m_waitingAck) {
    return;
  }
  m_socket->write(rdqt::packMessage(rdqt::MessageType::FsWriteFileReq, rdqt::makeWriteFilePayload(targetPath, data)));
}

void TcpFileClientWorker::requestFileChunk(const QString& path, qint64 offset, qint32 maxBytes) {
  if (!m_socket || m_socket->state() != QAbstractSocket::ConnectedState || m_waitingAck) {
    return;
  }
  m_socket->write(rdqt::packMessage(rdqt::MessageType::FsReadChunkReq, rdqt::makeReadChunkPayload(path, offset, maxBytes)));
}

void TcpFileClientWorker::sendFileChunk(const QString& targetPath, qint64 totalSize, qint64 offset, const QByteArray& data) {
  if (!m_socket || m_socket->state() != QAbstractSocket::ConnectedState || m_waitingAck) {
    return;
  }
  m_socket->write(
      rdqt::packMessage(rdqt::MessageType::FsWriteChunkReq, rdqt::makeWriteChunkPayload(targetPath, totalSize, offset, data)));
}

void TcpFileClientWorker::requestDeletePath(const QString& path, bool recursive) {
  if (!m_socket || m_socket->state() != QAbstractSocket::ConnectedState || m_waitingAck) {
    return;
  }
  m_socket->write(
      rdqt::packMessage(rdqt::MessageType::FsDeletePathReq, rdqt::makeDeletePathPayload(path, recursive)));
}

void TcpFileClientWorker::onReadyRead() {
  if (!m_socket) {
    return;
  }
  // TCP 是字节流，单次 readyRead 可能只到半包，也可能多个消息黏在一起，
  // 所以这里统一累积到缓冲区后循环按“8 字节头 + body 长度”拆包。
  m_readBuffer.append(m_socket->readAll());
  while (true) {
    QByteArray body;
    rdqt::MessageType type{};
    if (!tryParseMessage(body, type)) {
      break;
    }
    processMessage(type, body);
  }
}

void TcpFileClientWorker::onDisconnected() {
  emit logMessage(QStringLiteral("文件通道已断开"));
  emit connected(false, QStringLiteral("文件通道已断开"));
}

bool TcpFileClientWorker::tryParseMessage(QByteArray& body, rdqt::MessageType& typeOut) {
  if (m_readBuffer.size() < 8) {
    return false;
  }
  QDataStream head(m_readBuffer.left(8));
  head.setVersion(QDataStream::Qt_5_15);
  quint32 type = 0;
  quint32 len = 0;
  head >> type >> len;
  if (m_readBuffer.size() < 8 + static_cast<int>(len)) {
    return false;
  }
  body = m_readBuffer.mid(8, static_cast<int>(len));
  m_readBuffer.remove(0, 8 + static_cast<int>(len));
  typeOut = static_cast<rdqt::MessageType>(type);
  return true;
}

void TcpFileClientWorker::processMessage(rdqt::MessageType type, const QByteArray& body) {
  if (type == rdqt::MessageType::FileHelloAck) {
    QDataStream ds(body);
    ds.setVersion(QDataStream::Qt_5_15);
    quint8 ok = 0;
    QString reason;
    ds >> ok >> reason;
    m_waitingAck = false;
    emit connected(ok == 1, reason);
    if (ok == 1) {
      requestRoots();
    }
    return;
  }
  if (m_waitingAck) {
    return;
  }
  if (type == rdqt::MessageType::FsListRootsRsp) {
    QString basePath;
    QVector<rdqt::FileEntry> entries;
    if (rdqt::readFileEntriesPayload(body, &basePath, &entries)) {
      emit rootsListed(entries);
    }
    return;
  }
  if (type == rdqt::MessageType::FsListDirRsp) {
    QString basePath;
    QVector<rdqt::FileEntry> entries;
    if (rdqt::readFileEntriesPayload(body, &basePath, &entries)) {
      emit directoryListed(basePath, entries);
    }
    return;
  }
  if (type == rdqt::MessageType::FsReadFileRsp) {
    QDataStream ds(body);
    ds.setVersion(QDataStream::Qt_5_15);
    QString path;
    quint8 ok = 0;
    QString reason;
    QByteArray data;
    ds >> path >> ok >> reason >> data;
    emit fileReceived(path, ok == 1, reason, data);
    return;
  }
  if (type == rdqt::MessageType::FsWriteFileRsp) {
    QDataStream ds(body);
    ds.setVersion(QDataStream::Qt_5_15);
    QString path;
    quint8 ok = 0;
    QString reason;
    ds >> path >> ok >> reason;
    emit fileWritten(path, ok == 1, reason);
    return;
  }
  if (type == rdqt::MessageType::FsReadChunkRsp) {
    // 下载分片：服务端返回 path、文件总长、当前偏移、本片数据以及 done 标记，
    // 主线程收到后会把 data 写入本地文件并决定是否继续请求下一片。
    QDataStream ds(body);
    ds.setVersion(QDataStream::Qt_5_15);
    QString path;
    qint64 totalSize = 0;
    qint64 offset = 0;
    quint8 done = 0;
    quint8 ok = 0;
    QString reason;
    QByteArray data;
    ds >> path >> totalSize >> offset >> done >> ok >> reason >> data;
    emit fileChunkReceived(path, totalSize, offset, done == 1, ok == 1, reason, data);
    return;
  }
  if (type == rdqt::MessageType::FsWriteChunkRsp) {
    // 上传分片确认：服务端只回写“这一片写了多少、是否全部完成”，
    // 客户端以此推进本地读取偏移，不需要把数据再带回来。
    QDataStream ds(body);
    ds.setVersion(QDataStream::Qt_5_15);
    QString path;
    qint64 totalSize = 0;
    qint64 offset = 0;
    qint32 writtenBytes = 0;
    quint8 done = 0;
    quint8 ok = 0;
    QString reason;
    ds >> path >> totalSize >> offset >> writtenBytes >> done >> ok >> reason;
    emit fileChunkWritten(path, totalSize, offset, writtenBytes, done == 1, ok == 1, reason);
    return;
  }
  if (type == rdqt::MessageType::FsDeletePathRsp) {
    QDataStream ds(body);
    ds.setVersion(QDataStream::Qt_5_15);
    QString path;
    quint8 ok = 0;
    QString reason;
    ds >> path >> ok >> reason;
    emit pathDeleted(path, ok == 1, reason);
  }
}

void TcpFileClientWorker::ensureSocketCreated() {
  if (m_socket) {
    return;
  }
  // socket 必须在 worker 所在线程里创建，确保 readyRead / disconnected 等信号也在该线程触发。
  m_socket = new QTcpSocket(this);
  connect(m_socket, &QTcpSocket::readyRead, this, &TcpFileClientWorker::onReadyRead);
  connect(m_socket, &QTcpSocket::disconnected, this, &TcpFileClientWorker::onDisconnected);
}
