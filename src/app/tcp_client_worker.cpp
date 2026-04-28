#include "tcp_client_worker.h"

#include <QBuffer>
#include <QDataStream>
#include <QDateTime>

TcpClientWorker::TcpClientWorker(QObject* parent) : QObject(parent) {
}

void TcpClientWorker::connectToHost(const QString& ip,
                                    quint16 port,
                                    const QString& verifyCode,
                                    rdqt::QualityPreset preset) {
  m_readBuffer.clear();
  m_waitingAck = true;

  ensureSocketCreated();
  if (!m_socket) {
    emit connected(false, QStringLiteral("连接失败：网络模块初始化失败"));
    return;
  }

  m_socket->connectToHost(ip, port);
  if (!m_socket->waitForConnected(4000)) {
    emit connected(false, QStringLiteral("连接失败：%1").arg(m_socket->errorString()));
    return;
  }
  emit logMessage(QStringLiteral("TCP 已连接，发送验证码..."));
  const qint64 written =
      m_socket->write(rdqt::packMessage(rdqt::MessageType::Hello, rdqt::makeHelloPayload(verifyCode, preset)));
  emit logMessage(QStringLiteral("已发送握手包，写入字节=%1").arg(written));
}

void TcpClientWorker::disconnectHost() {
  if (m_socket) {
    m_socket->disconnectFromHost();
  }
}

void TcpClientWorker::sendInputEvent(const rdqt::RemoteInputEvent& event) {
  if (!m_socket || m_socket->state() != QAbstractSocket::ConnectedState || m_waitingAck) {
    return;
  }
  m_socket->write(rdqt::packMessage(rdqt::MessageType::InputEvent, rdqt::makeInputEventPayload(event)));
  ++m_inputSentCount;
  if (m_inputSentCount % 200 == 0) {
    emit logMessage(QStringLiteral("输入事件已发送累计=%1").arg(m_inputSentCount));
  }
}

void TcpClientWorker::sendPing(qint64 clientTsMs) {
  if (!m_socket || m_socket->state() != QAbstractSocket::ConnectedState || m_waitingAck) {
    return;
  }
  m_socket->write(rdqt::packMessage(rdqt::MessageType::Ping, rdqt::makePingPayload(clientTsMs)));
}

bool TcpClientWorker::tryParseMessage(QByteArray& body, rdqt::MessageType& typeOut) {
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

void TcpClientWorker::onReadyRead() {
  if (!m_socket) {
    return;
  }
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

void TcpClientWorker::processMessage(rdqt::MessageType type, const QByteArray& body) {
  QDataStream ds(body);
  ds.setVersion(QDataStream::Qt_5_15);

  if (type == rdqt::MessageType::HelloAck) {
    quint8 ok = 0;
    QString reason;
    ds >> ok >> reason;
    m_waitingAck = false;
    emit connected(ok == 1, reason);
    emit logMessage(QStringLiteral("收到握手应答：ok=%1，reason=%2").arg(ok).arg(reason));
    if (ok != 1) {
      m_socket->disconnectFromHost();
    }
    return;
  }

  if (m_waitingAck) {
    return;
  }

  if (type == rdqt::MessageType::FullFrame) {
    quint32 w = 0;
    quint32 h = 0;
    QByteArray jpegData;
    ds >> w >> h >> jpegData;

    QImage image;
    image.loadFromData(jpegData, "JPEG");
    if (!image.isNull()) {
      emit logMessage(QStringLiteral("收到整帧消息：%1x%2，jpeg=%3字节").arg(w).arg(h).arg(jpegData.size()));
      emit fullFrameArrived(image.convertToFormat(QImage::Format_ARGB32));
    } else {
      emit logMessage(QStringLiteral("整帧解码失败：jpeg=%1字节").arg(jpegData.size()));
    }
    return;
  }

  if (type == rdqt::MessageType::PatchFrame) {
    quint32 count = 0;
    ds >> count;
    QVector<rdqt::PatchBlock> patches;
    patches.reserve(static_cast<int>(count));
    for (quint32 i = 0; i < count; ++i) {
      qint32 x = 0, y = 0, w = 0, h = 0;
      QByteArray jpegData;
      ds >> x >> y >> w >> h >> jpegData;
      rdqt::PatchBlock p;
      p.rect = QRect(x, y, w, h);
      p.jpegData = jpegData;
      patches.push_back(std::move(p));
    }
    emit logMessage(QStringLiteral("收到补丁消息：块数=%1").arg(count));
    emit patchArrived(patches);
    return;
  }

  if (type == rdqt::MessageType::Pong) {
    qint64 sentTs = 0;
    ds >> sentTs;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    emit pingRttUpdated(static_cast<int>(qMax<qint64>(0, now - sentTs)));
  }
}

void TcpClientWorker::onDisconnected() {
  emit logMessage(QStringLiteral("连接已断开"));
  emit socketClosed();
}

void TcpClientWorker::ensureSocketCreated() {
  if (m_socket) {
    return;
  }
  // 让 QTcpSocket 在 worker 所在线程中构造，避免跨线程对象归属冲突。
  m_socket = new QTcpSocket(this);
  connect(m_socket, &QTcpSocket::readyRead, this, &TcpClientWorker::onReadyRead);
  connect(m_socket, &QTcpSocket::disconnected, this, &TcpClientWorker::onDisconnected);
}
