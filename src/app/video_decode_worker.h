#pragma once

#include <QByteArray>
#include <QImage>
#include <QObject>
#include <QSize>
#include <memory>

class FfmpegH264Decoder;

// 在独立线程中执行 FFmpeg H.264 解码，避免阻塞 Qt 主线程。
class VideoDecodeWorker : public QObject {
  Q_OBJECT
public:
  explicit VideoDecodeWorker(QObject* parent = nullptr);

public slots:
  void onVideoFrame(const QSize& logicalSize, bool keyFrame, qint64 ptsMs, const QByteArray& data);

signals:
  void frameDecoded(const QImage& image, const QSize& logicalSize, qint64 encodedBytes);
  void decodeLog(const QString& text);

private:
  std::unique_ptr<FfmpegH264Decoder> m_decoder;
};
