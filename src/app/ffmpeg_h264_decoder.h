#pragma once

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

AVPixelFormat ffmpeg_decoder_hw_format(AVCodecContext* avctx, const AVPixelFormat* pix_fmts);
}

class FfmpegH264Decoder {
  friend AVPixelFormat ffmpeg_decoder_hw_format(AVCodecContext*, const AVPixelFormat*);

public:
  FfmpegH264Decoder();
  ~FfmpegH264Decoder();

  bool isReady() const;
  QString lastError() const;
  QString backendDescription() const;

  bool open();
  void close();

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
