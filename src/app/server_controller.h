#pragma once

/**
 * @file server_controller.h
 * @brief UI 线程门面：装载 TcpServerWorker（网络线程）与 CaptureWorker（采集线程），校验通过后启动屏幕捕获并向客户端推流。
 */

#include "capture_worker.h"
#include "tcp_file_server_worker.h"
#include "tcp_server_worker.h"

#include <QObject>
#include <QThread>

/**
 * @class ServerController
 * @brief 线程模型：networkThread 跑 TcpServerWorker；captureThread 跑 CaptureWorker。
 *
 * clientAuthed 后 CaptureWorker::startCapture；帧数据 frameReady → TcpServerWorker::sendPacket。
 */
class ServerController : public QObject {
  Q_OBJECT
public:
  explicit ServerController(QObject* parent = nullptr);
  ~ServerController() override;

  /** 当前启动会话生成的数字验证码（界面展示供客户端填写）。 */
  QString verifyCode() const;

signals:
  void appendLog(const QString& text);
  /** 服务端捕获侧 QoS：FPS、码率（近似）、当前 JPEG 质量。 */
  void qosUpdated(int fps, int kbps, int jpegQuality);

public slots:
  /** 生成验证码并开始监听端口（异步在网络线程）。 */
  void startServer(quint16 port);
  /** 停止捕获并关闭监听、断开客户端。 */
  void stopServer();

private:
  QString generateVerifyCode() const;

private:
  QString m_verifyCode;
  QThread m_networkThread;
  QThread m_captureThread;
  TcpServerWorker* m_serverWorker = nullptr;
  TcpFileServerWorker* m_fileServerWorker = nullptr;
  CaptureWorker* m_captureWorker = nullptr;
};
