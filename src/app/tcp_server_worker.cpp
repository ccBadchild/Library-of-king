#include "tcp_server_worker.h"

#include <QDataStream>
#include <windows.h>

namespace {

DWORD mouseFlagFromButton(qint32 button, bool down) {
  if (button & Qt::LeftButton) {
    return down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
  }
  if (button & Qt::RightButton) {
    return down ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP;
  }
  if (button & Qt::MiddleButton) {
    return down ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP;
  }
  return 0;
}

} // namespace

TcpServerWorker::TcpServerWorker(QObject* parent) : QObject(parent) {
}

void TcpServerWorker::startListen(quint16 port, const QString& verifyCode) {
  ensureServerCreated();
  stopListen();
  m_verifyCode = verifyCode;
  if (!m_server || !m_server->listen(QHostAddress::Any, port)) {
    emit logMessage(QStringLiteral("监听失败：%1").arg(m_server ? m_server->errorString() : QStringLiteral("服务未初始化")));
    return;
  }
  emit logMessage(QStringLiteral("服务端已启动，监听端口：%1").arg(port));
}

void TcpServerWorker::stopListen() {
  resetClient();
  if (m_server && m_server->isListening()) {
    m_server->close();
  }
}

void TcpServerWorker::sendPacket(const QByteArray& packet) {
  if (!m_client || !m_clientVerified || m_client->state() != QAbstractSocket::ConnectedState) {
    return;
  }
  const qint64 written = m_client->write(packet);
  emit networkBacklogChanged(m_client->bytesToWrite());
  if (written <= 0) {
    emit logMessage(QStringLiteral("发送失败：socket 写入返回 %1").arg(written));
  }
}

void TcpServerWorker::onNewConnection() {
  if (!m_server) {
    return;
  }
  if (m_client) {
    QTcpSocket* old = m_server->nextPendingConnection();
    old->disconnectFromHost();
    old->deleteLater();
    return;
  }
  m_client = m_server->nextPendingConnection();
  m_clientVerified = false;
  m_readBuffer.clear();
  connect(m_client, &QTcpSocket::readyRead, this, &TcpServerWorker::onSocketReadyRead);
  connect(m_client, &QTcpSocket::disconnected, this, &TcpServerWorker::onSocketDisconnected);
  emit logMessage(QStringLiteral("客户端已连接，等待验证码校验..."));
}

bool TcpServerWorker::tryParseMessage(QByteArray& body, rdqt::MessageType& typeOut) {
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

void TcpServerWorker::onSocketReadyRead() {
  if (!m_client) {
    return;
  }

  m_readBuffer.append(m_client->readAll());

  while (true) {
    QByteArray body;
    rdqt::MessageType type{};
    if (!tryParseMessage(body, type)) {
      break;
    }

    if (!m_clientVerified) {
      if (type != rdqt::MessageType::Hello) {
        m_client->disconnectFromHost();
        return;
      }

      QDataStream ds(body);
      ds.setVersion(QDataStream::Qt_5_15);
      QString code;
      quint8 presetRaw = 1;
      ds >> code >> presetRaw;
      const bool ok = (code == m_verifyCode);
      const QString reason = ok ? QStringLiteral("校验成功") : QStringLiteral("验证码错误");
      m_client->write(rdqt::packMessage(rdqt::MessageType::HelloAck, rdqt::makeHelloAckPayload(ok, reason)));
      emit logMessage(QStringLiteral("收到握手请求：验证码长度=%1，清晰度=%2").arg(code.size()).arg(presetRaw));

      if (!ok) {
        emit logMessage(QStringLiteral("客户端验证码错误，已拒绝连接"));
        m_client->disconnectFromHost();
        return;
      }

      m_clientVerified = true;
      emit logMessage(QStringLiteral("客户端验证通过，开始推流"));
      emit clientAuthed(static_cast<rdqt::QualityPreset>(presetRaw));
      continue;
    }

    if (type == rdqt::MessageType::InputEvent) {
      applyRemoteInput(body);
      continue;
    }

    if (type == rdqt::MessageType::Ping) {
      m_client->write(rdqt::packMessage(rdqt::MessageType::Pong, body));
    }
  }
}

void TcpServerWorker::onSocketDisconnected() {
  emit logMessage(QStringLiteral("客户端已断开"));
  resetClient();
}

void TcpServerWorker::resetClient() {
  if (m_client) {
    m_client->deleteLater();
    m_client = nullptr;
  }
  m_clientVerified = false;
  m_readBuffer.clear();
}

void TcpServerWorker::ensureServerCreated() {
  if (m_server) {
    return;
  }
  // 确保 QTcpServer 在当前工作线程中创建，避免跨线程 parent/child 错误。
  m_server = new QTcpServer(this);
  connect(m_server, &QTcpServer::newConnection, this, &TcpServerWorker::onNewConnection);
}

void TcpServerWorker::applyRemoteInput(const QByteArray& body) {
  QDataStream ds(body);
  ds.setVersion(QDataStream::Qt_5_15);

  quint8 rawType = 0;
  qint32 x = 0, y = 0, delta = 0, key = 0, buttons = 0, modifiers = 0;
  ds >> rawType >> x >> y >> delta >> key >> buttons >> modifiers;
  Q_UNUSED(modifiers);
  const rdqt::InputEventType type = static_cast<rdqt::InputEventType>(rawType);

  INPUT in{};
  switch (type) {
    case rdqt::InputEventType::MouseMove:
      in.type = INPUT_MOUSE;
      in.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE;
      in.mi.dx = x;
      in.mi.dy = y;
      SendInput(1, &in, sizeof(INPUT));
      break;
    case rdqt::InputEventType::MouseDown:
    case rdqt::InputEventType::MouseUp:
      if (x >= 0 && y >= 0) {
        INPUT moveIn{};
        moveIn.type = INPUT_MOUSE;
        moveIn.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE;
        moveIn.mi.dx = x;
        moveIn.mi.dy = y;
        SendInput(1, &moveIn, sizeof(INPUT));
      }
      in.type = INPUT_MOUSE;
      in.mi.dwFlags = mouseFlagFromButton(buttons, type == rdqt::InputEventType::MouseDown);
      if (in.mi.dwFlags != 0) {
        SendInput(1, &in, sizeof(INPUT));
      }
      break;
    case rdqt::InputEventType::MouseWheel:
      in.type = INPUT_MOUSE;
      in.mi.dwFlags = MOUSEEVENTF_WHEEL;
      in.mi.mouseData = static_cast<DWORD>(delta);
      SendInput(1, &in, sizeof(INPUT));
      break;
    case rdqt::InputEventType::KeyDown:
    case rdqt::InputEventType::KeyUp:
      in.type = INPUT_KEYBOARD;
      in.ki.wVk = static_cast<WORD>(key);
      in.ki.dwFlags = (type == rdqt::InputEventType::KeyUp) ? KEYEVENTF_KEYUP : 0;
      SendInput(1, &in, sizeof(INPUT));
      break;
    default:
      break;
  }
}
