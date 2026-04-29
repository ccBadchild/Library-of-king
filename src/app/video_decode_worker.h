#pragma once

/**
 * @file video_decode_worker.h
 * @brief H.264 解码 Worker（ QObject 仅在 decode 线程）：接收 Annex B 片段，输出 RGB/QImage，避免阻塞 UI。
 */

#include <QByteArray>
#include <QImage>
#include <QObject>
#include <QSize>
#include <memory>

class FfmpegH264Decoder;

/**
 * @class VideoDecodeWorker
 * @brief onVideoFrame 槽绑定 TcpClientWorker::videoFrameArrived（QueuedConnection）；首次解码时惰性创建 FfmpegH264Decoder。
 */
class VideoDecodeWorker : public QObject {
  Q_OBJECT
public:
  explicit VideoDecodeWorker(QObject* parent = nullptr);

public slots:
  /** 接收一条编码帧（logicalSize 用于界面展示分辨率文案）。 */
  void onVideoFrame(const QSize& logicalSize, bool keyFrame, qint64 ptsMs, const QByteArray& data);

signals:
  /** 解码成功后回到 Controller 线程刷新界面；encodedBytes 用于 QoS 带宽估算。 */
  void frameDecoded(const QImage& image, const QSize& logicalSize, qint64 encodedBytes);
  void decodeLog(const QString& text);

private:
  std::unique_ptr<FfmpegH264Decoder> m_decoder;
};
