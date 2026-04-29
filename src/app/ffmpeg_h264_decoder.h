#pragma once

/**
 * @file ffmpeg_h264_decoder.h
 * @brief FFmpeg H.264 解码：优先 D3D11VA/DXVA2 硬件解码，失败回落软件解码；输出 Format_ARGB32 QImage。
 */

#include <QByteArray>
#include <QImage>
#include <QSize>
#include <QString>

extern "C" {
#include <libavutil/pixfmt.h>
struct AVCodec;
struct AVCodecContext;
struct AVFrame;
struct SwsContext;

/** FFmpeg get_format 回调：选择与本实例 hw_pix_fmt 匹配的硬件像素格式。 */
AVPixelFormat ffmpeg_decoder_hw_format(AVCodecContext* avctx, const AVPixelFormat* pix_fmts);
}

/**
 * @class FfmpegH264Decoder
 * @brief decode(packetData) 接收 Annex B；硬件帧可能需 hw_download → swscale → BGRA。
 */
class FfmpegH264Decoder {
  friend AVPixelFormat ffmpeg_decoder_hw_format(AVCodecContext*, const AVPixelFormat*);

public:
  FfmpegH264Decoder();
  ~FfmpegH264Decoder();

  bool isReady() const;
  QString lastError() const;
  QString backendDescription() const;

  /** 探测解码器并尝试创建硬件设备上下文。 */
  bool open();
  void close();

  /** 单次传入一段比特流；成功则 outImage 被写成 RGB32。 */
  bool decode(const QByteArray& packetData, QImage& outImage);

private:
  bool openSoftwareOnly(const AVCodec* codec);
  bool tryOpenHardware(const AVCodec* codec, int hwDeviceType);

  void setError(const QString& text);

private:
  AVCodecContext* m_ctx = nullptr;
  AVFrame* m_frame = nullptr;
  AVFrame* m_swTransfer = nullptr;
  AVFrame* m_bgra = nullptr;
  SwsContext* m_sws = nullptr;

  QSize m_lastSize{};
  AVPixelFormat m_hwPixFmt = AV_PIX_FMT_NONE;
  AVPixelFormat m_lastSrcPixFmt = AV_PIX_FMT_NONE;

  bool m_hwDecode = false;
  QString m_backendDesc;

  bool m_ready = false;
  QString m_error;
};
