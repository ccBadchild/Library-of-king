/**
 * @file client_controller.cpp
 * @brief ClientController 实现：双线程 worker、QoS/Ping 定时器、JPEG 补丁合并与重连策略。
 */

#include "client_controller.h"
#include "app_logger.h"
#include "video_decode_worker.h"

#include <QBuffer>
#include <QDateTime>
#include <QMetaObject>
#include <QPainter>

// ----------------------------------------------------------------------------
// 构造：创建 worker、搬迁至线程、连接日志/QoS/解码链路；线程最后在析构里 quit+wait。
// ----------------------------------------------------------------------------

ClientController::ClientController(QObject* parent) : QObject(parent) {
  m_worker = new TcpClientWorker();
  m_decodeWorker = new VideoDecodeWorker();

  m_worker->moveToThread(&m_networkThread);
  m_decodeWorker->moveToThread(&m_decodeThread);

  connect(&m_networkThread, &QThread::finished, m_worker, &QObject::deleteLater);
  connect(&m_decodeThread, &QThread::finished, m_decodeWorker, &QObject::deleteLater);

  m_reconnectTimer.setSingleShot(true);
  connect(&m_reconnectTimer, &QTimer::timeout, this, [this]() {
    if (!m_autoReconnect || m_userInitiatedDisconnect || m_lastIp.isEmpty() || m_lastPort == 0) {
      return;
    }
    ++m_reconnectCount;
    emit appendLog(QStringLiteral("自动重连第 %1 次...").arg(m_reconnectCount));
    startConnect(m_lastIp, m_lastPort, m_lastCode, m_lastPreset, m_lastCodec);
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
  connect(m_worker, &TcpClientWorker::connected, this, [this](bool ok, const QString& reason) {
    if (!ok &&
        (reason.contains(QStringLiteral("验证码错误")) || reason.contains(QStringLiteral("校验失败")))) {
      m_autoReconnect = false;
      m_reconnectTimer.stop();
    }
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

  // 网络线程收到 H.264 包 → 队列投递到解码线程；解码完成后队列回主线程更新 UI。
  connect(m_worker,
          &TcpClientWorker::videoFrameArrived,
          m_decodeWorker,
          &VideoDecodeWorker::onVideoFrame,
          Qt::QueuedConnection);
  connect(m_decodeWorker,
          &VideoDecodeWorker::frameDecoded,
          this,
          [this](const QImage& img, const QSize& logicalSize, qint64 encodedBytes) {
            m_currentFrame = img;
            m_lastResolution =
                QStringLiteral("%1x%2").arg(logicalSize.width()).arg(logicalSize.height());
            ++m_recvFramesWindow;
            m_recvBytesWindow += encodedBytes;
            emit frameUpdated(m_currentFrame);
          },
          Qt::QueuedConnection);
  connect(m_decodeWorker, &VideoDecodeWorker::decodeLog, this, &ClientController::appendLog);

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

  m_decodeThread.start();
  m_networkThread.start();
}

// 析构顺序：先发停止连接 → 断开跨线程 decode 槽 → 停解码线程再停网络线程（避免仍在投递帧）。
ClientController::~ClientController() {
  stopConnect();

  disconnect(m_worker, &TcpClientWorker::videoFrameArrived, m_decodeWorker, nullptr);

  m_decodeThread.quit();
  m_decodeThread.wait();

  m_networkThread.quit();
  m_networkThread.wait();
}

// startConnect：清空上一帧图像并重置 QoS 文案；实际 TCP 握身在 worker 线程。
void ClientController::startConnect(const QString& ip,
                                    quint16 port,
                                    const QString& code,
                                    rdqt::QualityPreset preset,
                                    rdqt::VideoCodec codec) {
  m_autoReconnect = true;
  m_userInitiatedDisconnect = false;
  m_reconnectTimer.stop();
  m_currentFrame = QImage();
  m_lastIp = ip;
  m_lastPort = port;
  m_lastCode = code;
  m_lastPreset = preset;
  m_lastCodec = codec;
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
      [this, ip, port, code, preset, codec]() { m_worker->connectToHost(ip, port, code, preset, codec); },
      Qt::QueuedConnection);
}

void ClientController::stopConnect() {
  m_userInitiatedDisconnect = true;
  m_autoReconnect = false;
  m_reconnectTimer.stop();
  applog::write(applog::Role::Client, QStringLiteral("收到断开连接请求"));
  QMetaObject::invokeMethod(m_worker, [this]() { m_worker->disconnectHost(); }, Qt::QueuedConnection);
}

void ClientController::sendRemoteInput(const rdqt::RemoteInputEvent& event) {
  QMetaObject::invokeMethod(
      m_worker, [this, event]() { m_worker->sendInputEvent(event); }, Qt::QueuedConnection);
}

// 将 JPEG 小块解码后绘制到当前完整帧对应矩形区域。
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
