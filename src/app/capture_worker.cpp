/**
 * @file capture_worker.cpp
 * @brief GDI 抓屏、缩放、JPEG/H264 封装与 QoS 窗口统计；积压过大时主动降质量与拉长帧间隔。
 */

#include "capture_worker.h"
#include "ffmpeg_h264_encoder.h"

#include <QBuffer>
#include <QDateTime>
#include <QElapsedTimer>
#include <QThread>
#include <cstring>
#include <windows.h>

namespace {

/**
 * BitBlt + GetDIBits 抓取主显示器整屏到 QImage。
 * 步骤：取得屏幕 DC → 兼容内存 DC → DDB 位图 → BitBlt 拷贝像素 → GetDIBits 落成 ARGB32。
 */
QImage captureDesktopImage() {
  // ① 使用虚拟屏幕宽高（单显示器场景即整屏分辨率）。
  const int width = GetSystemMetrics(SM_CXSCREEN);
  const int height = GetSystemMetrics(SM_CYSCREEN);
  if (width <= 0 || height <= 0) {
    return QImage();
  }

  // ② GetDC(nullptr) 拿到「整个屏幕」的设备上下文。
  HDC screenDc = GetDC(nullptr);
  // ③ CreateCompatibleDC：创建与屏幕兼容的内存 DC，后续在位图上绘图。
  HDC memDc = CreateCompatibleDC(screenDc);
  // ④ CreateCompatibleBitmap：创建与屏幕色彩深度兼容的空位图（容器）。
  HBITMAP bmp = CreateCompatibleBitmap(screenDc, width, height);
  // ⑤ SelectObject：把位图选入内存 DC，后续 BitBlt 的目标是位图像素缓冲区。
  HGDIOBJ oldObj = SelectObject(memDc, bmp);

  // ⑥ BitBlt：从屏幕 DC 拷贝矩形像素到内存 DC（到位图）；CAPTUREBLT 包含分层窗口等内容。
  if (!BitBlt(memDc, 0, 0, width, height, screenDc, 0, 0, SRCCOPY | CAPTUREBLT)) {
    SelectObject(memDc, oldObj);
    DeleteObject(bmp);
    DeleteDC(memDc);
    ReleaseDC(nullptr, screenDc);
    return QImage();
  }

  // ⑦ BITMAPINFO：描述输出为自上而下（biHeight 为负）、每像素 32 位 BI_RGB。
  BITMAPINFO bmi{};
  bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bmi.bmiHeader.biWidth = width;
  bmi.bmiHeader.biHeight = -height;
  bmi.bmiHeader.biPlanes = 1;
  bmi.bmiHeader.biBitCount = 32;
  bmi.bmiHeader.biCompression = BI_RGB;

  // ⑧ 预分配 QImage ARGB32，bits() 指向连续缓冲区供 GetDIBits 填充。
  QImage out(width, height, QImage::Format_ARGB32);
  // ⑨ GetDIBits：把 DDB 中的像素转为设备无关位图格式写入 bits()。
  const int lines = GetDIBits(memDc, bmp, 0, static_cast<UINT>(height), out.bits(), &bmi, DIB_RGB_COLORS);

  // ⑩ 恢复 DC、删除 GDI 对象、释放屏幕 DC。
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
  // 记录清晰度档位 → JPEG 量化质量与缩放比例（scaleSizeFromPreset）。
  m_preset = preset;
  m_codec = codec;
  m_jpegQuality = rdqt::jpegQualityFromPreset(preset);
  m_h264.reset();
  m_prevScaledFrame = QImage();
  m_hasSentFullFrame = false;
  m_frameCounter = 0;
  // 目标帧率非法时用默认 12fps；帧间隔 ≥16ms，避免过高的 CPU 占用。
  m_targetFps = (fps <= 0) ? 12 : fps;
  const int interval = qMax(16, 1000 / m_targetFps);
  m_sentBytesWindow = 0;
  m_sentFramesWindow = 0;
  m_lastQosEmitMs = 0;
  m_running.store(true);
  // 独立线程跑 captureLoop，避免阻塞 QObject 所在线程。
  m_loopThread = std::thread(&CaptureWorker::captureLoop, this, interval);
  emit logMessage(QStringLiteral("采集线程已启动，FPS=%1，codec=%2，JPEG质量=%3")
                      .arg(fps)
                      .arg(static_cast<int>(m_codec))
                      .arg(m_jpegQuality));
  emit logMessage(QStringLiteral("采集循环参数：interval=%1ms").arg(interval));
}

void CaptureWorker::stopCapture() {
  // 原子置 false → captureLoop while 退出 → join 等待线程结束。
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
    // 轻微休眠避免 while 空转占满核心（interval 仍为节拍主导）。
    QThread::msleep(1);

    const qint64 nowMs = wallClock.elapsed();
    // TcpServerWorker 报告的待发字节：积压大则降 JPEG 质量、拉长帧间隔；反之小幅恢复。
    const qint64 backlog = m_networkBacklog.load();
    if (backlog > 3 * 1024 * 1024) {
      m_jpegQuality = qMax(30, m_jpegQuality - 2);
      intervalMs = qMin(120, intervalMs + 4);
    } else if (backlog < 256 * 1024) {
      m_jpegQuality = qMin(85, m_jpegQuality + 1);
      intervalMs = qMax(16, intervalMs - 1);
    }

    // 未到下一帧节拍则跳过本次采集。
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
    // 每秒聚合一次 QoS：窗口内发送帧数 → FPS，字节×8 → 近似 kbps。
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
  // Qt 内置 JPEG 编码（依赖插件）；quality 越高画质越好、体积越大。
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

  // 固定 64×64 网格：逐块逐行 memcmp，像素完全一致则跳过；否则将该块 JPEG 压缩后装入 PatchBlock。
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
  // ① GDI 抓整屏原始图。
  const QImage raw = captureDesktopImage();
  if (raw.isNull()) {
    emit logMessage(QStringLiteral("抓屏失败：GDI 返回空图像"));
    return;
  }

  // ② 按清晰度预设缩放到逻辑分辨率，统一为 ARGB32 供编码器/比较使用。
  const QSize targetSize = rdqt::scaleSizeFromPreset(raw.size(), m_preset);
  const QImage scaled = raw.scaled(targetSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                            .convertToFormat(QImage::Format_ARGB32);

  // ③ H.264 分支：按需创建 FfmpegH264Encoder，open 失败则把 m_codec 切回 JPEG。
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
        emit logMessage(QStringLiteral("H.264 编码已就绪：后端=%1，分辨率=%2x%3，fps=%4，码率=%5 kbps")
                            .arg(m_h264->backendDescription())
                            .arg(scaled.width())
                            .arg(scaled.height())
                            .arg(m_targetFps)
                            .arg(kbps));
      }
    }

    // ④ encode 产出若干 Annex B 包 → 每条封装为 VideoFrame 消息发出。
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

  // ⑤ JPEG 分支 — 首轮或分辨率变化：必须发 FullFrame，客户端才能有基底画布。
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

  // ⑥ 后续帧：脏块检测 → 补丁过多则改发整帧，否则 PatchFrame。
  QVector<rdqt::PatchBlock> patches = buildDirtyPatches(scaled, m_prevScaledFrame, m_jpegQuality);
  const int blockCount = patches.size();
  const int totalBlocks = ((scaled.width() + 63) / 64) * ((scaled.height() + 63) / 64);

  // 无变化则不发送（节省带宽）。
  if (blockCount == 0) {
    return;
  }
  // 脏块超过网格一半以上：补丁开销大，不如整帧 JPEG。
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
  // ⑦ 更新上一帧副本供下一帧差异比对。
  m_prevScaledFrame = scaled;
}

void CaptureWorker::onNetworkBacklogChanged(qint64 bytes) {
  // TcpServerWorker 侧 socket bytesToWrite()，原子写入供 captureLoop 自适应调节。
  m_networkBacklog.store(bytes);
}
