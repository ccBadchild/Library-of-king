#pragma once

/**
 * @file tcp_server_worker.h
 * @brief 运行在专属线程内的 TCP 服务端：监听端口、校验 Hello、转发 Ping/Pong，并把远端输入注入 Windows SendInput。
 */

#include "protocol_qt.h"

#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>

/**
 * @class TcpServerWorker
 * @brief 单客户端模型：新连接到来时若已有客户端则丢弃第二个连接。
 *
 * networkBacklogChanged：socket 待发缓冲区字节数，供 CaptureWorker 动态调节画质与帧间隔。
 */
class TcpServerWorker : public QObject {
  Q_OBJECT
public:
  explicit TcpServerWorker(QObject* parent = nullptr);

signals:
  /** 校验通过后发射，携带客户端请求的清晰度与协商后的编码。 */
  void clientAuthed(rdqt::QualityPreset preset, rdqt::VideoCodec codec);
  void logMessage(const QString& text);
  void networkBacklogChanged(qint64 bytes);

public slots:
  void startListen(quint16 port, const QString& verifyCode);
  void stopListen();
  /** CaptureWorker 输出的已打包二进制帧（可直接 write）。 */
  void sendPacket(const QByteArray& packet);

private slots:
  void onNewConnection();
  void onSocketReadyRead();
  void onSocketDisconnected();

private:
  bool tryParseMessage(QByteArray& body, rdqt::MessageType& typeOut);
  void resetClient();
  void ensureServerCreated();
  /** 按 RemoteInputEvent 载荷调用 Win32 SendInput。 */
  void applyRemoteInput(const QByteArray& body);

private:
  QTcpServer* m_server = nullptr;
  QTcpSocket* m_client = nullptr;
  QByteArray m_readBuffer;
  QString m_verifyCode;
  bool m_clientVerified = false;
  rdqt::VideoCodec m_clientCodec = rdqt::VideoCodec::Jpeg;
  quint64 m_inputRecvCount = 0;
};
