#pragma once

#include "protocol_qt.h"

#include <QObject>
#include <atomic>
#include <memory>
#include <thread>

class FfmpegH264Encoder;

class CaptureWorker : public QObject {
  Q_OBJECT
public:
  explicit CaptureWorker(QObject* parent = nullptr);
  ~CaptureWorker() override;

signals:
  void frameReady(const QByteArray& packet);
  void logMessage(const QString& text);
  void qosUpdated(int fps, int kbps, int jpegQuality);

public slots:
  void startCapture(rdqt::QualityPreset preset, int fps, rdqt::VideoCodec codec);
  void stopCapture();
  void onNetworkBacklogChanged(qint64 bytes);

private:
  void captureLoop(int intervalMs);
  void captureOnce();
  QByteArray encodeJpeg(const QImage& img, int quality) const;
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
  QImage m_prevScaledFrame;
  bool m_hasSentFullFrame = false;
  int m_frameCounter = 0;
};
