#include "ffmpeg_h264_decoder.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/error.h>
#include <libswscale/swscale.h>
}

namespace {

QString ffErr(int err) {
  char buf[AV_ERROR_MAX_STRING_SIZE]{};
  av_strerror(err, buf, sizeof(buf));
  return QString::fromUtf8(buf);
}

} // namespace

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

void FfmpegH264Decoder::setError(const QString& text) {
  m_error = text;
}

bool FfmpegH264Decoder::open() {
  close();
  const AVCodec* codec = avcodec_find_decoder(AV_CODEC_ID_H264);
  if (!codec) {
    setError(QStringLiteral("找不到 H.264 解码器"));
    return false;
  }
  m_ctx = avcodec_alloc_context3(codec);
  if (!m_ctx) {
    setError(QStringLiteral("avcodec_alloc_context3 失败"));
    return false;
  }
  m_ctx->thread_count = 1;
  const int rc = avcodec_open2(m_ctx, codec, nullptr);
  if (rc < 0) {
    setError(QStringLiteral("avcodec_open2 失败：%1").arg(ffErr(rc)));
    close();
    return false;
  }
  m_frame = av_frame_alloc();
  m_bgra = av_frame_alloc();
  if (!m_frame || !m_bgra) {
    setError(QStringLiteral("av_frame_alloc 失败"));
    close();
    return false;
  }
  m_ready = true;
  return true;
}

void FfmpegH264Decoder::close() {
  m_ready = false;
  if (m_sws) {
    sws_freeContext(m_sws);
    m_sws = nullptr;
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

  const int sendRc = avcodec_send_packet(m_ctx, pkt);
  av_packet_free(&pkt);
  if (sendRc < 0) {
    setError(QStringLiteral("avcodec_send_packet 失败：%1").arg(ffErr(sendRc)));
    return false;
  }

  const int recvRc = avcodec_receive_frame(m_ctx, m_frame);
  if (recvRc == AVERROR(EAGAIN) || recvRc == AVERROR_EOF) {
    return false;
  }
  if (recvRc < 0) {
    setError(QStringLiteral("avcodec_receive_frame 失败：%1").arg(ffErr(recvRc)));
    return false;
  }

  const QSize sz(m_frame->width, m_frame->height);
  if (sz.isEmpty()) {
    return false;
  }

  if (m_lastSize != sz || !m_sws) {
    if (m_sws) {
      sws_freeContext(m_sws);
      m_sws = nullptr;
    }
    m_sws = sws_getContext(m_frame->width,
                           m_frame->height,
                           static_cast<AVPixelFormat>(m_frame->format),
                           m_frame->width,
                           m_frame->height,
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
    m_bgra->width = m_frame->width;
    m_bgra->height = m_frame->height;
    const int bufRc = av_frame_get_buffer(m_bgra, 32);
    if (bufRc < 0) {
      setError(QStringLiteral("av_frame_get_buffer(BGRA) 失败：%1").arg(ffErr(bufRc)));
      return false;
    }
    m_lastSize = sz;
  }

  const int writableRc = av_frame_make_writable(m_bgra);
  if (writableRc < 0) {
    setError(QStringLiteral("av_frame_make_writable(BGRA) 失败：%1").arg(ffErr(writableRc)));
    return false;
  }

  sws_scale(m_sws, m_frame->data, m_frame->linesize, 0, m_frame->height, m_bgra->data, m_bgra->linesize);

  QImage img(m_bgra->data[0], m_bgra->width, m_bgra->height, m_bgra->linesize[0], QImage::Format_ARGB32);
  outImage = img.copy(); // 复制，避免引用 FFmpeg buffer 生命周期问题
  return true;
}

