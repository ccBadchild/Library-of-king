#pragma once

#include <QByteArray>
#include <QImage>
#include <QSize>
#include <QString>

struct AVCodecContext;
struct AVFrame;
struct SwsContext;

class FfmpegH264Decoder {
public:
  FfmpegH264Decoder();
  ~FfmpegH264Decoder();

  bool isReady() const;
  QString lastError() const;

  bool open();
  void close();

  // 返回 true 表示成功产出了一帧图像
  bool decode(const QByteArray& packetData, QImage& outImage);

private:
  void setError(const QString& text);

private:
  AVCodecContext* m_ctx = nullptr;
  AVFrame* m_frame = nullptr;
  AVFrame* m_bgra = nullptr;
  SwsContext* m_sws = nullptr;
  QSize m_lastSize;
  bool m_ready = false;
  QString m_error;
};

