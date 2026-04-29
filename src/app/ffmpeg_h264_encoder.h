#pragma once

/**
 * @file ffmpeg_h264_encoder.h
 * @brief FFmpeg/libavcodec H.264 编码封装：优先 NVENC / AMF / QSV，失败回落 libx264；ARGB → YUV420 → 编码。
 */

#include <QByteArray>
#include <QImage>
#include <QString>
#include <QVector>

struct AVCodec;
struct AVCodecContext;
struct AVFrame;
struct SwsContext;

/**
 * @class FfmpegH264Encoder
 * @brief 非线程安全实例：仅在 CaptureWorker 的采集线程内调用 encode。
 */
class FfmpegH264Encoder {
public:
  struct Packet {
    QByteArray data;
    bool keyFrame = false;
    qint64 ptsMs = 0;
  };

  FfmpegH264Encoder();
  ~FfmpegH264Encoder();

  bool isReady() const;
  QString lastError() const;
  /** 当前实际选用的编码器名称摘要（NVENC / AMF / QSV / libx264）。 */
  QString backendDescription() const;

  /** width×height，fps，目标视频码率（kbps）；内部尝试硬件编码器链表。 */
  bool open(const QSize& size, int fps, int kbps);
  void close();

  /** 输入必须为 Format_ARGB32；返回若干 Annex B NAL 片段（可能含多包）。 */
  QVector<Packet> encode(const QImage& argb32Frame, qint64 ptsMs);

private:
  bool tryOpenCodec(const AVCodec* codec, const QString& backendLabel, const QSize& size, int fps, int kbps);
  static void applyCodecOptions(AVCodecContext* ctx, const char* codecName);

  void setError(const QString& text);

private:
  AVCodecContext* m_ctx = nullptr;
  AVFrame* m_frame = nullptr;
  SwsContext* m_sws = nullptr;
  QSize m_size;
  int m_fps = 30;
  bool m_ready = false;
  QString m_error;
  QString m_backendDesc;
  qint64 m_frameIndex = 0;
};
