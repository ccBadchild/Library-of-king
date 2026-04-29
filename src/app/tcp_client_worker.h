#pragma once

/**
 * @file tcp_client_worker.h
 * @brief 运行在专属 QThread 内的 TCP 客户端：握手、接收服务端 JPEG/H264/Pong，并向服务端发送输入事件与 Ping。
 */

#include "protocol_qt.h"

#include <QObject>
#include <QTcpSocket>

/**
 * @class TcpClientWorker
 * @brief QTcpSocket 必须在构造线程内创建（ensureSocketCreated）；解析逻辑见 tryParseMessage + processMessage。
 *
 * m_waitingAck：在收到 HelloAck 之前丢弃其它业务消息，防止乱序干扰。
 */
class TcpClientWorker : public QObject {
  Q_OBJECT
public:
  explicit TcpClientWorker(QObject* parent = nullptr);

signals:
  /** HelloAck 解析结果（失败时常附带断开）。 */
  void connected(bool ok, const QString& reason);
  void logMessage(const QString& text);
  /** JPEG 整帧（已解码为 QImage）。 */
  void fullFrameArrived(const QImage& image);
  /** JPEG 脏块列表（由上层 ClientController 合并）。 */
  void patchArrived(const QVector<rdqt::PatchBlock>& patches);
  /** H.264 Annex B 数据块（交由解码线程）。 */
  void videoFrameArrived(const QSize& size, bool keyFrame, qint64 ptsMs, const QByteArray& data);
  /** TCP 断开（触发 ClientController 自动重连逻辑）。 */
  void socketClosed();
  /** Ping/Pong 计算的毫秒往返延迟。 */
  void pingRttUpdated(int ms);

public slots:
  void connectToHost(const QString& ip,
                     quint16 port,
                     const QString& verifyCode,
                     rdqt::QualityPreset preset,
                     rdqt::VideoCodec codec);
  void disconnectHost();
  void sendInputEvent(const rdqt::RemoteInputEvent& event);
  /** 负载内携带客户端发送 Ping 的时刻戳（毫秒）。 */
  void sendPing(qint64 clientTsMs);

private slots:
  void onReadyRead();
  void onDisconnected();

private:
  /** 从 m_readBuffer 尝试剥出一条完整消息；不足则返回 false。 */
  bool tryParseMessage(QByteArray& body, rdqt::MessageType& typeOut);
  void processMessage(rdqt::MessageType type, const QByteArray& body);
  /** 延迟创建 socket 并绑定信号槽（必须在 worker 线程）。 */
  void ensureSocketCreated();

private:
  QTcpSocket* m_socket = nullptr;
  QByteArray m_readBuffer;
  /** 未收到 HelloAck 前为 true，禁止发送输入/Ping（握手未完成）。 */
  bool m_waitingAck = false;
  quint64 m_inputSentCount = 0;
};
