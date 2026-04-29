/**
 * @file video_decode_worker.cpp
 * @brief H.264 NAL 解码入口：惰性初始化解码器，失败通过 decodeLog 反馈 UI。
 */

#include "video_decode_worker.h"
#include "ffmpeg_h264_decoder.h"

VideoDecodeWorker::VideoDecodeWorker(QObject* parent) : QObject(parent) {}

void VideoDecodeWorker::onVideoFrame(const QSize& logicalSize,
                                     bool /*keyFrame*/,
                                     qint64 /*ptsMs*/,
                                     const QByteArray& data) {
  if (data.isEmpty()) {
    return;
  }

  if (!m_decoder) {
    m_decoder = std::make_unique<FfmpegH264Decoder>();
    if (!m_decoder->open()) {
      emit decodeLog(QStringLiteral("H.264 解码器初始化失败：%1").arg(m_decoder->lastError()));
      m_decoder.reset();
      return;
    }
    emit decodeLog(QStringLiteral("H.264 解码后端：%1").arg(m_decoder->backendDescription()));
  }

  QImage img;
  if (!m_decoder->decode(data, img) || img.isNull()) {
    return;
  }

  emit frameDecoded(img, logicalSize, static_cast<qint64>(data.size()));
}
