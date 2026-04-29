#include "capture_worker.h"
#include "ffmpeg_h264_encoder.h"

#include <QBuffer>
#include <QDateTime>
#include <QElapsedTimer>
#include <QThread>
#include <cstring>
#include <windows.h>

namespace {

// 在工作线程中使用 GDI 抓屏，避免 QScreen/QPixmap 的 GUI 线程限制。
QImage captureDesktopImage() {
  const int width = GetSystemMetrics(SM_CXSCREEN);
  const int height = GetSystemMetrics(SM_CYSCREEN);
  if (width <= 0 || height <= 0) {
    return QImage();
  }

  HDC screenDc = GetDC(nullptr);
  HDC memDc = CreateCompatibleDC(screenDc);
  HBITMAP bmp = CreateCompatibleBitmap(screenDc, width, height);
  HGDIOBJ oldObj = SelectObject(memDc, bmp);

  if (!BitBlt(memDc, 0, 0, width, height, screenDc, 0, 0, SRCCOPY | CAPTUREBLT)) {
    SelectObject(memDc, oldObj);
    DeleteObject(bmp);
    DeleteDC(memDc);
    ReleaseDC(nullptr, screenDc);
    return QImage();
  }

  BITMAPINFO bmi{};
  bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bmi.bmiHeader.biWidth = width;
  bmi.bmiHeader.biHeight = -height;
  bmi.bmiHeader.biPlanes = 1;
  bmi.bmiHeader.biBitCount = 32;
  bmi.bmiHeader.biCompression = BI_RGB;

  QImage out(width, height, QImage::Format_ARGB32);
  const int lines = GetDIBits(memDc, bmp, 0, static_cast<UINT>(height), out.bits(), &bmi, DIB_RGB_COLORS);

  SelectObject(memDc, oldObj);
  DeleteObject(bmp);
  DeleteDC(memDc);
  ReleaseDC(nullptr, screenDc);

  if (lines != height) {
    return QImage();
  }
  return out;
}

} // namespace

CaptureWorker::CaptureWorker(QObject* parent) : QObject(parent) {
}

CaptureWorker::~CaptureWorker() {
  stopCapture();
}

void CaptureWorker::startCapture(rdqt::QualityPreset preset, int fps, rdqt::VideoCodec codec) {
  stopCapture();
  m_preset = preset;
  m_codec = codec;
  m_jpegQuality = rdqt::jpegQualityFromPreset(preset);
  m_h264.reset();
  m_prevScaledFrame = QImage();
  m_hasSentFullFrame = false;
  m_frameCounter = 0;
  m_targetFps = (fps <= 0) ? 12 : fps;
  const int interval = qMax(16, 1000 / m_targetFps);
  m_sentBytesWindow = 0;
  m_sentFramesWindow = 0;
  m_lastQosEmitMs = 0;
  m_running.store(true);
  m_loopThread = std::thread(&CaptureWorker::captureLoop, this, interval);
  emit logMessage(QStringLiteral("采集线程已启动，FPS=%1，codec=%2，JPEG质量=%3")
                      .arg(fps)
                      .arg(static_cast<int>(m_codec))
                      .arg(m_jpegQuality));
  emit logMessage(QStringLiteral("采集循环参数：interval=%1ms").arg(interval));
}

void CaptureWorker::stopCapture() {
  const bool wasRunning = m_running.exchange(false);
  if (m_loopThread.joinable()) {
    m_loopThread.join();
  }
  m_h264.reset();
  m_prevScaledFrame = QImage();
  m_hasSentFullFrame = false;
  if (wasRunning) {
    emit logMessage(QStringLiteral("采集线程已停止"));
  }
}

void CaptureWorker::captureLoop(int intervalMs) {
  QElapsedTimer wallClock;
  wallClock.start();
  qint64 lastCaptureMs = wallClock.elapsed();

  while (m_running.load()) {
    QThread::msleep(1);

    const qint64 nowMs = wallClock.elapsed();
    const qint64 backlog = m_networkBacklog.load();
    if (backlog > 3 * 1024 * 1024) {
      m_jpegQuality = qMax(30, m_jpegQuality - 2);
      intervalMs = qMin(120, intervalMs + 4);
    } else if (backlog < 256 * 1024) {
      m_jpegQuality = qMin(85, m_jpegQuality + 1);
      intervalMs = qMax(16, intervalMs - 1);
    }

    if (nowMs - lastCaptureMs < intervalMs) {
      continue;
    }

    QElapsedTimer frameCostTimer;
    frameCostTimer.start();
    captureOnce();
    const qint64 frameCost = frameCostTimer.elapsed();
    if (frameCost > 100) {
      emit logMessage(QStringLiteral("性能告警：单帧采集+编码耗时=%1ms").arg(frameCost));
    }
    if (nowMs - m_lastQosEmitMs >= 1000) {
      const int fps = m_sentFramesWindow;
      const int kbps = static_cast<int>((m_sentBytesWindow * 8) / 1024);
      emit qosUpdated(fps, kbps, m_jpegQuality);
      m_sentBytesWindow = 0;
      m_sentFramesWindow = 0;
      m_lastQosEmitMs = nowMs;
    }
    lastCaptureMs = nowMs;
  }
}

QByteArray CaptureWorker::encodeJpeg(const QImage& img, int quality) const {
  QByteArray buf;
  QBuffer out(&buf);
  out.open(QIODevice::WriteOnly);
  img.save(&out, "JPEG", quality);
  return buf;
}

QVector<rdqt::PatchBlock> CaptureWorker::buildDirtyPatches(const QImage& current,
                                                           const QImage& previous,
                                                           int quality) const {
  QVector<rdqt::PatchBlock> patches;
  if (current.size() != previous.size() || current.isNull() || previous.isNull()) {
    return patches;
  }

  // 按固定网格做“脏块检测”，仅传输变化区域。
  const int blockSize = 64;
  for (int y = 0; y < current.height(); y += blockSize) {
    for (int x = 0; x < current.width(); x += blockSize) {
      const QRect rect(x, y, qMin(blockSize, current.width() - x), qMin(blockSize, current.height() - y));
      bool changed = false;
      for (int row = 0; row < rect.height(); ++row) {
        const QRgb* cLine = reinterpret_cast<const QRgb*>(current.constScanLine(rect.y() + row)) + rect.x();
        const QRgb* pLine = reinterpret_cast<const QRgb*>(previous.constScanLine(rect.y() + row)) + rect.x();
        if (memcmp(cLine, pLine, static_cast<size_t>(rect.width()) * sizeof(QRgb)) != 0) {
          changed = true;
          break;
        }
      }
      if (!changed) {
        continue;
      }
      rdqt::PatchBlock block;
      block.rect = rect;
      block.jpegData = encodeJpeg(current.copy(rect), quality);
      patches.push_back(std::move(block));
    }
  }
  return patches;
}

void CaptureWorker::captureOnce() {
  const QImage raw = captureDesktopImage();
  if (raw.isNull()) {
    emit logMessage(QStringLiteral("抓屏失败：GDI 返回空图像"));
    return;
  }

  const QSize targetSize = rdqt::scaleSizeFromPreset(raw.size(), m_preset);
  const QImage scaled = raw.scaled(targetSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                            .convertToFormat(QImage::Format_ARGB32);

  if (m_codec == rdqt::VideoCodec::H264) {
    if (!m_h264) {
      m_h264 = std::make_unique<FfmpegH264Encoder>();
    }
    if (!m_h264->isReady()) {
      int kbps = 1200;
      switch (m_preset) {
        case rdqt::QualityPreset::Low:
          kbps = 700;
          break;
        case rdqt::QualityPreset::High:
          kbps = 2500;
          break;
        default:
          kbps = 1200;
          break;
      }
      if (!m_h264->open(scaled.size(), m_targetFps, kbps)) {
        emit logMessage(QStringLiteral("H.264 编码器初始化失败：%1，回退 JPEG").arg(m_h264->lastError()));
        m_codec = rdqt::VideoCodec::Jpeg;
      } else {
        emit logMessage(QStringLiteral("H.264 编码器已就绪：%1x%2 fps=%3 kbps=%4")
                            .arg(scaled.width())
                            .arg(scaled.height())
                            .arg(m_targetFps)
                            .arg(kbps));
      }
    }

    if (m_codec == rdqt::VideoCodec::H264 && m_h264 && m_h264->isReady()) {
      const qint64 ptsMs = QDateTime::currentMSecsSinceEpoch();
      const auto packets = m_h264->encode(scaled, ptsMs);
      for (const auto& p : packets) {
        const QByteArray payload = rdqt::makeVideoFramePayload(scaled.size(), p.keyFrame, p.ptsMs, p.data);
        emit frameReady(rdqt::packMessage(rdqt::MessageType::VideoFrame, payload));
        m_sentBytesWindow += payload.size();
        ++m_sentFramesWindow;
      }
      ++m_frameCounter;
      return;
    }
  }

  if (!m_hasSentFullFrame || m_prevScaledFrame.isNull() || m_prevScaledFrame.size() != scaled.size()) {
    const QByteArray jpeg = encodeJpeg(scaled, m_jpegQuality);
    const QByteArray fullPayload = rdqt::makeFullFramePayload(scaled.size(), jpeg);
    emit frameReady(rdqt::packMessage(rdqt::MessageType::FullFrame, fullPayload));
    m_sentBytesWindow += fullPayload.size();
    ++m_sentFramesWindow;
    m_prevScaledFrame = scaled;
    m_hasSentFullFrame = true;
    ++m_frameCounter;
    emit logMessage(QStringLiteral("发送整帧 #%1，分辨率=%2x%3，JPEG=%4字节")
                        .arg(m_frameCounter)
                        .arg(scaled.width())
                        .arg(scaled.height())
                        .arg(jpeg.size()));
    return;
  }

  QVector<rdqt::PatchBlock> patches = buildDirtyPatches(scaled, m_prevScaledFrame, m_jpegQuality);
  const int blockCount = patches.size();
  const int totalBlocks = ((scaled.width() + 63) / 64) * ((scaled.height() + 63) / 64);

  // 若变化范围过大，则回退为整帧发送，避免补丁包过多导致低效。
  if (blockCount == 0) {
    return;
  }
  if (blockCount > totalBlocks * 0.55) {
    const QByteArray jpeg = encodeJpeg(scaled, m_jpegQuality);
    const QByteArray fullPayload = rdqt::makeFullFramePayload(scaled.size(), jpeg);
    emit frameReady(rdqt::packMessage(rdqt::MessageType::FullFrame, fullPayload));
    m_sentBytesWindow += fullPayload.size();
    ++m_sentFramesWindow;
    ++m_frameCounter;
    if (m_frameCounter % 30 == 0) {
      emit logMessage(QStringLiteral("变化较大，回退整帧发送，当前累计帧=%1").arg(m_frameCounter));
    }
  } else {
    const QByteArray patchPayload = rdqt::makePatchPayload(patches);
    emit frameReady(rdqt::packMessage(rdqt::MessageType::PatchFrame, patchPayload));
    m_sentBytesWindow += patchPayload.size();
    ++m_sentFramesWindow;
    ++m_frameCounter;
    if (m_frameCounter % 30 == 0) {
      emit logMessage(QStringLiteral("发送补丁帧，脏块数量=%1，当前累计帧=%2").arg(blockCount).arg(m_frameCounter));
    }
  }
  m_prevScaledFrame = scaled;
}

void CaptureWorker::onNetworkBacklogChanged(qint64 bytes) {
  m_networkBacklog.store(bytes);
}

