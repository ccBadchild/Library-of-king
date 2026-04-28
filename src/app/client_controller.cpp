#include "client_controller.h"
#include "app_logger.h"

#include <QBuffer>
#include <QDateTime>
#include <QMetaObject>
#include <QPainter>

ClientController::ClientController(QObject* parent) : QObject(parent) {
  m_worker = new TcpClientWorker();
  m_worker->moveToThread(&m_networkThread);
  connect(&m_networkThread, &QThread::finished, m_worker, &QObject::deleteLater);

  m_reconnectTimer.setSingleShot(true);
  connect(&m_reconnectTimer, &QTimer::timeout, this, [this]() {
    if (!m_autoReconnect || m_userInitiatedDisconnect || m_lastIp.isEmpty() || m_lastPort == 0) {
      return;
    }
    ++m_reconnectCount;
    emit appendLog(QStringLiteral("自动重连第 %1 次...").arg(m_reconnectCount));
    startConnect(m_lastIp, m_lastPort, m_lastCode, m_lastPreset);
  });

  m_qosTimer.setInterval(1000);
  connect(&m_qosTimer, &QTimer::timeout, this, [this]() {
    emit qosUpdated(m_recvFramesWindow,
                    static_cast<int>((m_recvBytesWindow * 8) / 1024),
                    m_reconnectCount,
                    m_lastLatencyMs,
                    m_lastResolution);
    m_recvFramesWindow = 0;
    m_recvBytesWindow = 0;
  });
  m_qosTimer.start();

  m_pingTimer.setInterval(1000);
  connect(&m_pingTimer, &QTimer::timeout, this, [this]() {
    QMetaObject::invokeMethod(
        m_worker,
        [this]() { m_worker->sendPing(QDateTime::currentMSecsSinceEpoch()); },
        Qt::QueuedConnection);
  });
  m_pingTimer.start();

  connect(m_worker, &TcpClientWorker::logMessage, this, &ClientController::appendLog);
  connect(m_worker, &TcpClientWorker::logMessage, this, [](const QString& text) {
    applog::write(applog::Role::Client, text);
  });
  connect(m_worker, &TcpClientWorker::connected, this, &ClientController::stateChanged);
  connect(m_worker, &TcpClientWorker::connected, this, [](bool ok, const QString& reason) {
    applog::write(applog::Role::Client,
                  ok ? QStringLiteral("连接握手成功：%1").arg(reason)
                     : QStringLiteral("连接握手失败：%1").arg(reason));
  });
  connect(m_worker, &TcpClientWorker::fullFrameArrived, this, [this](const QImage& image) {
    m_currentFrame = image;
    m_lastResolution = QStringLiteral("%1x%2").arg(image.width()).arg(image.height());
    ++m_recvFramesWindow;
    m_recvBytesWindow += static_cast<qint64>(image.sizeInBytes());
    applog::write(applog::Role::Client,
                  QStringLiteral("收到整帧，分辨率=%1x%2").arg(image.width()).arg(image.height()));
    emit frameUpdated(m_currentFrame);
  });
  connect(m_worker, &TcpClientWorker::patchArrived, this, [this](const QVector<rdqt::PatchBlock>& patches) {
    applyPatches(patches);
    ++m_recvFramesWindow;
    m_recvBytesWindow += static_cast<qint64>(patches.size()) * 16 * 1024;
    applog::write(applog::Role::Client, QStringLiteral("收到补丁帧，脏块数量=%1").arg(patches.size()));
    emit frameUpdated(m_currentFrame);
  });
  connect(m_worker, &TcpClientWorker::socketClosed, this, [this]() {
    applog::write(applog::Role::Client, QStringLiteral("Socket 已关闭"));
    emit stateChanged(false, QStringLiteral("连接已关闭"));
    if (!m_userInitiatedDisconnect && m_autoReconnect) {
      m_reconnectTimer.start(1500);
    }
  });
  connect(m_worker, &TcpClientWorker::pingRttUpdated, this, [this](int ms) {
    m_lastLatencyMs = ms;
  });

  m_networkThread.start();
}

ClientController::~ClientController() {
  stopConnect();
  m_networkThread.quit();
  m_networkThread.wait();
}

void ClientController::startConnect(const QString& ip, quint16 port, const QString& code, rdqt::QualityPreset preset) {
  m_userInitiatedDisconnect = false;
  m_reconnectTimer.stop();
  m_currentFrame = QImage();
  m_lastIp = ip;
  m_lastPort = port;
  m_lastCode = code;
  m_lastPreset = preset;
  m_lastLatencyMs = -1;
  m_lastResolution = "-";
  applog::write(applog::Role::Client,
                QStringLiteral("开始连接：%1:%2，验证码长度=%3，清晰度=%4")
                    .arg(ip)
                    .arg(port)
                    .arg(code.size())
                    .arg(static_cast<int>(preset)));
  QMetaObject::invokeMethod(
      m_worker,
      [this, ip, port, code, preset]() { m_worker->connectToHost(ip, port, code, preset); },
      Qt::QueuedConnection);
}

void ClientController::stopConnect() {
  m_userInitiatedDisconnect = true;
  m_reconnectTimer.stop();
  applog::write(applog::Role::Client, QStringLiteral("收到断开连接请求"));
  QMetaObject::invokeMethod(m_worker, [this]() { m_worker->disconnectHost(); }, Qt::QueuedConnection);
}

void ClientController::sendRemoteInput(const rdqt::RemoteInputEvent& event) {
  QMetaObject::invokeMethod(
      m_worker, [this, event]() { m_worker->sendInputEvent(event); }, Qt::QueuedConnection);
}

void ClientController::applyPatches(const QVector<rdqt::PatchBlock>& patches) {
  if (m_currentFrame.isNull()) {
    applog::write(applog::Role::Client, QStringLiteral("忽略补丁帧：当前尚未收到整帧"));
    return;
  }
  QPainter painter(&m_currentFrame);
  for (const rdqt::PatchBlock& p : patches) {
    QImage part;
    part.loadFromData(p.jpegData, "JPEG");
    if (part.isNull()) {
      continue;
    }
    painter.drawImage(p.rect, part);
  }
}
