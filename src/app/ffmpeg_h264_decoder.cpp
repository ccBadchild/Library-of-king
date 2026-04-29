/**
 * @file ffmpeg_h264_decoder.cpp
 * @brief libavcodec 解码 H.264（硬件优先）；ffmpeg_decoder_hw_format 供解码器挑选 HW 像素格式。
 */

#include "ffmpeg_h264_decoder.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/error.h>
#include <libavutil/hwcontext.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

namespace {

QString ffErr(int err) {
  char buf[AV_ERROR_MAX_STRING_SIZE]{};
  av_strerror(err, buf, sizeof(buf));
  return QString::fromUtf8(buf);
}

QString hwTypeLabel(int hwDeviceType) {
  switch (hwDeviceType) {
    case AV_HWDEVICE_TYPE_D3D11VA:
      return QStringLiteral("D3D11VA");
    case AV_HWDEVICE_TYPE_DXVA2:
      return QStringLiteral("DXVA2");
    default:
      return QStringLiteral("HW(%1)").arg(hwDeviceType);
  }
}

} // namespace

/**
 * FFmpeg 在解码器需要选择硬件像素格式时回调：遍历 pix_fmts，选出与本实例 m_hwPixFmt 一致的一项。
 */
extern "C" AVPixelFormat ffmpeg_decoder_hw_format(AVCodecContext* avctx, const AVPixelFormat* pix_fmts) {
  auto* self = static_cast<FfmpegH264Decoder*>(avctx->opaque);
  if (!self) {
    return AV_PIX_FMT_NONE;
  }
  for (const AVPixelFormat* p = pix_fmts; *p != AV_PIX_FMT_NONE; ++p) {
    if (*p == self->m_hwPixFmt) {
      return *p;
    }
  }
  return AV_PIX_FMT_NONE;
}

FfmpegH264Decoder::FfmpegH264Decoder() = default;

FfmpegH264Decoder::~FfmpegH264Decoder() {
  close();
}

bool FfmpegH264Decoder::isReady() const {
  return m_ready;
}

QString FfmpegH264Decoder::lastError() const {
  return m_error;
}

QString FfmpegH264Decoder::backendDescription() const {
  return m_backendDesc;
}

void FfmpegH264Decoder::setError(const QString& text) {
  m_error = text;
}

/**
 * 硬件解码路径：
 * ① avcodec_get_hw_config 枚举编码器支持的 HW config，匹配 device_type 且支持 DEVICE_CTX；
 * ② av_hwdevice_ctx_create 创建设备句柄；
 * ③ avcodec_alloc_context3 + hw_device_ctx + get_format + avcodec_open2；
 * ④ 额外分配 m_swTransfer：用于 av_hwframe_transfer_data 把 GPU 帧拷到内存。
 */
bool FfmpegH264Decoder::tryOpenHardware(const AVCodec* codec, int hwDeviceType) {
  const auto type = static_cast<AVHWDeviceType>(hwDeviceType);

  m_hwPixFmt = AV_PIX_FMT_NONE;
  for (int i = 0;; ++i) {
    const AVCodecHWConfig* cfg = avcodec_get_hw_config(codec, i);
    if (!cfg) {
      break;
    }
    if (cfg->device_type != type) {
      continue;
    }
    if (!(cfg->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX)) {
      continue;
    }
    m_hwPixFmt = cfg->pix_fmt;
    break;
  }
  if (m_hwPixFmt == AV_PIX_FMT_NONE) {
    return false;
  }

  AVBufferRef* hwDev = nullptr;
  const int devRc = av_hwdevice_ctx_create(&hwDev, type, nullptr, nullptr, 0);
  if (devRc < 0) {
    return false;
  }

  m_ctx = avcodec_alloc_context3(codec);
  if (!m_ctx) {
    av_buffer_unref(&hwDev);
    return false;
  }

  m_ctx->hw_device_ctx = av_buffer_ref(hwDev);
  av_buffer_unref(&hwDev);

  // opaque 指向 this，供 ffmpeg_decoder_hw_format 读取 m_hwPixFmt。
  m_ctx->opaque = this;
  m_ctx->get_format = ffmpeg_decoder_hw_format;
  m_ctx->thread_count = 1;

  const int openRc = avcodec_open2(m_ctx, codec, nullptr);
  if (openRc < 0) {
    avcodec_free_context(&m_ctx);
    m_ctx = nullptr;
    m_hwPixFmt = AV_PIX_FMT_NONE;
    return false;
  }

  m_frame = av_frame_alloc();
  m_swTransfer = av_frame_alloc();
  m_bgra = av_frame_alloc();
  if (!m_frame || !m_swTransfer || !m_bgra) {
    setError(QStringLiteral("av_frame_alloc 失败"));
    close();
    return false;
  }

  m_hwDecode = true;
  m_backendDesc = hwTypeLabel(hwDeviceType);
  m_ready = true;
  return true;
}

/** 软件解码：无 HW device；解码输出通常为 YUV，后续统一 swscale → BGRA。 */
bool FfmpegH264Decoder::openSoftwareOnly(const AVCodec* codec) {
  m_ctx = avcodec_alloc_context3(codec);
  if (!m_ctx) {
    setError(QStringLiteral("avcodec_alloc_context3 失败"));
    return false;
  }
  m_ctx->thread_count = 1;
  const int rc = avcodec_open2(m_ctx, codec, nullptr);
  if (rc < 0) {
    setError(QStringLiteral("avcodec_open2 失败：%1").arg(ffErr(rc)));
    avcodec_free_context(&m_ctx);
    m_ctx = nullptr;
    return false;
  }

  m_frame = av_frame_alloc();
  m_bgra = av_frame_alloc();
  if (!m_frame || !m_bgra) {
    setError(QStringLiteral("av_frame_alloc 失败"));
    close();
    return false;
  }

  m_hwDecode = false;
  m_backendDesc = QStringLiteral("software");
  m_ready = true;
  return true;
}

bool FfmpegH264Decoder::open() {
  close();

  const AVCodec* codec = avcodec_find_decoder(AV_CODEC_ID_H264);
  if (!codec) {
    setError(QStringLiteral("找不到 H.264 解码器"));
    return false;
  }

#ifdef _WIN32
  // Windows 下依次尝试 D3D11VA、DXVA2；均失败则 openSoftwareOnly。
  const int hwTypes[] = {
      AV_HWDEVICE_TYPE_D3D11VA,
      AV_HWDEVICE_TYPE_DXVA2,
  };
  for (int ht : hwTypes) {
    if (tryOpenHardware(codec, ht)) {
      return true;
    }
  }
#endif

  return openSoftwareOnly(codec);
}

void FfmpegH264Decoder::close() {
  m_ready = false;
  m_hwDecode = false;
  m_hwPixFmt = AV_PIX_FMT_NONE;
  m_lastSrcPixFmt = AV_PIX_FMT_NONE;
  m_backendDesc.clear();

  if (m_sws) {
    sws_freeContext(m_sws);
    m_sws = nullptr;
  }
  if (m_swTransfer) {
    av_frame_free(&m_swTransfer);
    m_swTransfer = nullptr;
  }
  if (m_bgra) {
    av_frame_free(&m_bgra);
    m_bgra = nullptr;
  }
  if (m_frame) {
    av_frame_free(&m_frame);
    m_frame = nullptr;
  }
  if (m_ctx) {
    avcodec_free_context(&m_ctx);
    m_ctx = nullptr;
  }
  m_lastSize = QSize();
}

bool FfmpegH264Decoder::decode(const QByteArray& packetData, QImage& outImage) {
  if (!m_ready || !m_ctx || !m_frame || !m_bgra) {
    return false;
  }
  if (packetData.isEmpty()) {
    return false;
  }

  // ① 把 Annex B 字节封装进 AVPacket（一次解码调用对应一块送进解码器的负载）。
  AVPacket* pkt = av_packet_alloc();
  if (!pkt) {
    return false;
  }
  const int pktRc = av_new_packet(pkt, packetData.size());
  if (pktRc < 0) {
    av_packet_free(&pkt);
    setError(QStringLiteral("av_new_packet 失败：%1").arg(ffErr(pktRc)));
    return false;
  }
  memcpy(pkt->data, packetData.constData(), static_cast<size_t>(packetData.size()));

  // ② send_packet：压缩包入解码器内部缓冲。
  const int sendRc = avcodec_send_packet(m_ctx, pkt);
  av_packet_free(&pkt);
  if (sendRc < 0) {
    setError(QStringLiteral("avcodec_send_packet 失败：%1").arg(ffErr(sendRc)));
    return false;
  }

  // ③ receive_frame：取出一帧未压缩图像（可能仍是 HW 表面像素格式）。
  const int recvRc = avcodec_receive_frame(m_ctx, m_frame);
  if (recvRc == AVERROR(EAGAIN) || recvRc == AVERROR_EOF) {
    return false;
  }
  if (recvRc < 0) {
    setError(QStringLiteral("avcodec_receive_frame 失败：%1").arg(ffErr(recvRc)));
    return false;
  }

  // ④ 若为硬件解码：先把帧从 GPU 转到 CPU 可读的 NV12/YUV（m_swTransfer）。
  AVFrame* srcForSws = m_frame;
  if (m_hwDecode) {
    if (!m_swTransfer) {
      return false;
    }
    av_frame_unref(m_swTransfer);
    const int tr = av_hwframe_transfer_data(m_swTransfer, m_frame, 0);
    if (tr < 0) {
      setError(QStringLiteral("av_hwframe_transfer_data 失败：%1").arg(ffErr(tr)));
      return false;
    }
    srcForSws = m_swTransfer;
  }

  const QSize sz(srcForSws->width, srcForSws->height);
  if (sz.isEmpty()) {
    return false;
  }

  const AVPixelFormat srcFmt = static_cast<AVPixelFormat>(srcForSws->format);
  // ⑤ 分辨率或源像素格式变化时重建 SwsContext，并为 m_bgra 分配缓冲区。
  if (m_lastSize != sz || m_lastSrcPixFmt != srcFmt || !m_sws) {
    if (m_sws) {
      sws_freeContext(m_sws);
      m_sws = nullptr;
    }
    m_sws = sws_getContext(srcForSws->width,
                           srcForSws->height,
                           srcFmt,
                           srcForSws->width,
                           srcForSws->height,
                           AV_PIX_FMT_BGRA,
                           SWS_FAST_BILINEAR,
                           nullptr,
                           nullptr,
                           nullptr);
    if (!m_sws) {
      setError(QStringLiteral("sws_getContext 失败"));
      return false;
    }

    m_bgra->format = AV_PIX_FMT_BGRA;
    m_bgra->width = srcForSws->width;
    m_bgra->height = srcForSws->height;
    const int bufRc = av_frame_get_buffer(m_bgra, 32);
    if (bufRc < 0) {
      setError(QStringLiteral("av_frame_get_buffer(BGRA) 失败：%1").arg(ffErr(bufRc)));
      return false;
    }
    m_lastSize = sz;
    m_lastSrcPixFmt = srcFmt;
  }

  const int writableRc = av_frame_make_writable(m_bgra);
  if (writableRc < 0) {
    setError(QStringLiteral("av_frame_make_writable(BGRA) 失败：%1").arg(ffErr(writableRc)));
    return false;
  }

  // ⑥ sws_scale：源平面 → BGRA，写入 m_bgra。
  sws_scale(m_sws,
            srcForSws->data,
            srcForSws->linesize,
            0,
            srcForSws->height,
            m_bgra->data,
            m_bgra->linesize);

  // ⑦ QImage 包装解码缓冲区（copy）交给调用方，避免 AVFrame 生命周期问题。
  QImage img(m_bgra->data[0], m_bgra->width, m_bgra->height, m_bgra->linesize[0], QImage::Format_ARGB32);
  outImage = img.copy();
  return true;
}
