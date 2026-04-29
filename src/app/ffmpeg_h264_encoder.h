#pragma once

#include <QByteArray>
#include <QImage>
#include <QString>
#include <QVector>

struct AVCodec;
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
  /** 当前编码后端说明，如 NVENC / AMF / QSV / software (libx264) */
  QString backendDescription() const;

  bool open(const QSize& size, int fps, int kbps);
  void close();

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
