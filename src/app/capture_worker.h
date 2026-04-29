#pragma once

/**
 * @file capture_worker.h
 * @brief 采集线程内的屏幕抓取（GDI）：支持 JPEG 整帧/补丁与 FFmpeg H.264，根据网络积压自适应画质与帧间隔。
 */

#include "protocol_qt.h"

#include <QObject>
#include <atomic>
#include <memory>
#include <thread>

class FfmpegH264Encoder;

/**
 * @class CaptureWorker
 * @brief std::thread 内循环 captureLoop → captureOnce：严禁在此线程调用 QObject::moveToThread 之外的 GUI API。
 *
 * H.264 失败时降级 JPEG；JPEG 路径先尝试 PatchFrame，大块变化比例过高则改发 FullFrame。
 */
class CaptureWorker : public QObject {
  Q_OBJECT
public:
  explicit CaptureWorker(QObject* parent = nullptr);
  ~CaptureWorker() override;

signals:
  /** 供 TcpServerWorker::sendPacket 发送的二进制帧（已 packMessage）。 */
  void frameReady(const QByteArray& packet);
  void logMessage(const QString& text);
  /** 每秒一发：FPS、码率近似、当前 JPEG 动态质量。 */
  void qosUpdated(int fps, int kbps, int jpegQuality);

public slots:
  /** 启动采集线程循环（preset/fps/codec）。 */
  void startCapture(rdqt::QualityPreset preset, int fps, rdqt::VideoCodec codec);
  /** join 线程并释放编码器状态。 */
  void stopCapture();
  /** TcpServerWorker 报告的待发字节数，用于自适应调节 m_jpegQuality 与 intervalMs。 */
  void onNetworkBacklogChanged(qint64 bytes);

private:
  /** 定时唤醒抓帧（intervalMs 可被积压算法动态拉大）。 */
  void captureLoop(int intervalMs);
  /** 单次抓屏 → 缩放 → H264 或 JPEG 路径封装 packet。 */
  void captureOnce();
  QByteArray encodeJpeg(const QImage& img, int quality) const;
  /** 64×64 网格像素比对脏块并各自 JPEG。 */
  QVector<rdqt::PatchBlock> buildDirtyPatches(const QImage& current, const QImage& previous, int quality) const;

private:
  std::atomic<bool> m_running{false};
  std::thread m_loopThread;
  rdqt::QualityPreset m_preset = rdqt::QualityPreset::Medium;
  rdqt::VideoCodec m_codec = rdqt::VideoCodec::Jpeg;
  std::unique_ptr<FfmpegH264Encoder> m_h264;
  int m_jpegQuality = 60;
  int m_targetFps = 12;
  std::atomic<qint64> m_networkBacklog{0};
  qint64 m_sentBytesWindow = 0;
  int m_sentFramesWindow = 0;
  qint64 m_lastQosEmitMs = 0;
  /** 上一帧缩放后的副本，用于差异补丁。 */
  QImage m_prevScaledFrame;
  bool m_hasSentFullFrame = false;
  int m_frameCounter = 0;
};
