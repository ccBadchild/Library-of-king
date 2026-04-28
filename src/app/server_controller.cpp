#include "server_controller.h"
#include "app_logger.h"

#include <QDateTime>
#include <QMetaObject>
#include <QRandomGenerator>

ServerController::ServerController(QObject* parent) : QObject(parent) {
  m_serverWorker = new TcpServerWorker();
  m_captureWorker = new CaptureWorker();

  m_serverWorker->moveToThread(&m_networkThread);
  m_captureWorker->moveToThread(&m_captureThread);

  connect(&m_networkThread, &QThread::finished, m_serverWorker, &QObject::deleteLater);
  connect(&m_captureThread, &QThread::finished, m_captureWorker, &QObject::deleteLater);

  connect(m_serverWorker, &TcpServerWorker::logMessage, this, &ServerController::appendLog);
  connect(m_serverWorker, &TcpServerWorker::logMessage, this, [](const QString& text) {
    applog::write(applog::Role::Server, text);
  });
  connect(m_captureWorker, &CaptureWorker::logMessage, this, &ServerController::appendLog);
  connect(m_captureWorker, &CaptureWorker::logMessage, this, [](const QString& text) {
    applog::write(applog::Role::Server, text);
  });
  connect(m_serverWorker, &TcpServerWorker::clientAuthed, m_captureWorker, [this](rdqt::QualityPreset preset) {
    QMetaObject::invokeMethod(
        m_captureWorker, [this, preset]() { m_captureWorker->startCapture(preset, 12); }, Qt::QueuedConnection);
  });
  connect(m_serverWorker,
          &TcpServerWorker::networkBacklogChanged,
          m_captureWorker,
          &CaptureWorker::onNetworkBacklogChanged,
          Qt::QueuedConnection);
  connect(m_captureWorker, &CaptureWorker::qosUpdated, this, &ServerController::qosUpdated);
  connect(m_serverWorker, &TcpServerWorker::clientAuthed, this, [](rdqt::QualityPreset preset) {
    applog::write(applog::Role::Server, QStringLiteral("收到 clientAuthed 信号，清晰度=%1").arg(static_cast<int>(preset)));
  });
  connect(m_captureWorker, &CaptureWorker::frameReady, m_serverWorker, &TcpServerWorker::sendPacket, Qt::QueuedConnection);

  m_networkThread.start();
  m_captureThread.start();
}

ServerController::~ServerController() {
  stopServer();
  m_networkThread.quit();
  m_captureThread.quit();
  m_networkThread.wait();
  m_captureThread.wait();
}

QString ServerController::verifyCode() const {
  return m_verifyCode;
}

QString ServerController::generateVerifyCode() const {
  const int num = QRandomGenerator::global()->bounded(100000, 999999);
  return QString::number(num);
}

void ServerController::startServer(quint16 port) {
  m_verifyCode = generateVerifyCode();
  emit appendLog(QStringLiteral("生成本次连接验证码：%1").arg(m_verifyCode));
  applog::write(applog::Role::Server, QStringLiteral("生成连接验证码：%1").arg(m_verifyCode));
  QMetaObject::invokeMethod(
      m_serverWorker,
      [this, port]() { m_serverWorker->startListen(port, m_verifyCode); },
      Qt::QueuedConnection);
}

void ServerController::stopServer() {
  applog::write(applog::Role::Server, QStringLiteral("收到停止服务端请求"));
  QMetaObject::invokeMethod(m_captureWorker, [this]() { m_captureWorker->stopCapture(); }, Qt::QueuedConnection);
  QMetaObject::invokeMethod(m_serverWorker, [this]() { m_serverWorker->stopListen(); }, Qt::QueuedConnection);
}
