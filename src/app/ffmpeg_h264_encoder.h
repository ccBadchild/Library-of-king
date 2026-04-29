#pragma once

#include <QByteArray>
#include <QImage>
#include <QSize>
#include <QVector>

struct AVCodecContext;
struct AVFrame;
struct SwsContext;

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

  bool open(const QSize& size, int fps, int kbps);
  void close();

  QVector<Packet> encode(const QImage& argb32Frame, qint64 ptsMs);

private:
  void setError(const QString& text);

private:
  AVCodecContext* m_ctx = nullptr;
  AVFrame* m_frame = nullptr;
  SwsContext* m_sws = nullptr;
  QSize m_size;
  int m_fps = 30;
  bool m_ready = false;
  QString m_error;
  qint64 m_frameIndex = 0;
};

