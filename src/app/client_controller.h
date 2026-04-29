#pragma once

/**
 * @file client_controller.h
 * @brief UI 线程可见的客户端门面：驱动 TcpClientWorker（网络线程）与 VideoDecodeWorker（解码线程），对外发射解码后的帧与连接状态。
 */

#include "protocol_qt.h"
#include "tcp_client_worker.h"

#include <QObject>
#include <QThread>
#include <QTimer>

class VideoDecodeWorker;

/**
 * @class ClientController
 * @brief 聚合两条 QThread：network（TcpClientWorker）、decode（VideoDecodeWorker）。
 *
 * - JPEG：fullFrameArrived / patchArrived 在主线程拼装 QImage；
 * - H.264：videoFrameArrived → decodeWorker → frameDecoded；
 * - 自动重连：socketClosed 且非用户主动断开时延迟再次 startConnect；验证码错误则关闭自动重连。
 */
class ClientController : public QObject {
  Q_OBJECT
public:
  explicit ClientController(QObject* parent = nullptr);
  ~ClientController() override;

signals:
  /** 追加到界面日志区（含 TcpClientWorker / VideoDecodeWorker 转发）。 */
  void appendLog(const QString& text);
  /** 解码或 JPEG 拼装后的 RGB 帧，供远程桌面控件显示。 */
  void frameUpdated(const QImage& image);
  /** TCP 握手结果或断开通知（connected=false）。 */
  void stateChanged(bool connected, const QString& reason);
  /** 每秒 QoS：FPS、码率估算、重连次数、Ping RTT、分辨率文案。 */
  void qosUpdated(int fps, int kbps, int reconnectCount, int latencyMs, const QString& resolution);

public slots:
  /** 异步连接到远端（invokeMethod 到网络线程）。 */
  void startConnect(const QString& ip,
                    quint16 port,
                    const QString& code,
                    rdqt::QualityPreset preset,
                    rdqt::VideoCodec codec = rdqt::VideoCodec::Auto);
  /** 用户主动断开：禁止自动重连。 */
  void stopConnect();
  /** 远程输入封装后经 TcpClientWorker 发往服务端。 */
  void sendRemoteInput(const rdqt::RemoteInputEvent& event);

private:
  /** 将补丁 JPEG 贴回 m_currentFrame（依赖已有整帧基底）。 */
  void applyPatches(const QVector<rdqt::PatchBlock>& patches);

private:
  QThread m_networkThread;
  QThread m_decodeThread;
  TcpClientWorker* m_worker = nullptr;
  VideoDecodeWorker* m_decodeWorker = nullptr;
  /** 当前展示的完整帧（补丁在此基础上绘制）。 */
  QImage m_currentFrame;
  /** 握手失败后延迟自动重连的单次定时器。 */
  QTimer m_reconnectTimer;
  QString m_lastIp;
  QString m_lastCode;
  quint16 m_lastPort = 0;
  rdqt::QualityPreset m_lastPreset = rdqt::QualityPreset::Medium;
  rdqt::VideoCodec m_lastCodec = rdqt::VideoCodec::Auto;
  /** 验证码错误等场景下设 false，阻止无限重连。 */
  bool m_autoReconnect = true;
  /** stopConnect() 置 true，区分被动掉线与主动断开。 */
  bool m_userInitiatedDisconnect = false;
  /** 统计窗口内已完成的成功连接次数（含自动重连）。 */
  int m_reconnectCount = 0;
  /** QoS 一秒窗口：收到的帧数及字节累计（JPEG/H264 路径分别累加）。 */
  int m_recvFramesWindow = 0;
  qint64 m_recvBytesWindow = 0;
  QTimer m_qosTimer;
  /** 周期性发送 Ping，配合服务端 Pong 更新 RTT。 */
  QTimer m_pingTimer;
  int m_lastLatencyMs = -1;
  QString m_lastResolution = "-";
};
