#include "ffmpeg_h264_encoder.h"

#include <QString>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/error.h>
#include <libswscale/swscale.h>
}

namespace {

QString ffErr(int err) {
  char buf[AV_ERROR_MAX_STRING_SIZE]{};
  av_strerror(err, buf, sizeof(buf));
  return QString::fromUtf8(buf);
}

int bitrateFromKbps(int kbps) {
  return qMax(64, kbps) * 1000;
}

} // namespace

FfmpegH264Encoder::FfmpegH264Encoder() = default;

FfmpegH264Encoder::~FfmpegH264Encoder() {
  close();
}

bool FfmpegH264Encoder::isReady() const {
  return m_ready;
}

QString FfmpegH264Encoder::lastError() const {
  return m_error;
}

void FfmpegH264Encoder::setError(const QString& text) {
  m_error = text;
}

bool FfmpegH264Encoder::open(const QSize& size, int fps, int kbps) {
  close();
  m_size = size;
  m_fps = qMax(1, fps);
  m_frameIndex = 0;

  const AVCodec* codec = avcodec_find_encoder_by_name("libx264");
  if (!codec) {
    codec = avcodec_find_encoder(AV_CODEC_ID_H264);
  }
  if (!codec) {
    setError(QStringLiteral("找不到 H.264 编码器（libx264/AV_CODEC_ID_H264）"));
    return false;
  }

  m_ctx = avcodec_alloc_context3(codec);
  if (!m_ctx) {
    setError(QStringLiteral("avcodec_alloc_context3 失败"));
    return false;
  }

  m_ctx->width = size.width();
  m_ctx->height = size.height();
  m_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
  m_ctx->time_base = AVRational{1, m_fps};
  m_ctx->framerate = AVRational{m_fps, 1};
  m_ctx->gop_size = m_fps;       // 约 1 秒一个关键帧
  m_ctx->max_b_frames = 0;       // 低延迟
  m_ctx->bit_rate = bitrateFromKbps(kbps);
  m_ctx->thread_count = 1;       // 延迟优先（避免多线程带来的缓冲）

  // x264 低延迟参数（如果是 libx264 会生效）
  av_opt_set(m_ctx->priv_data, "preset", "ultrafast", 0);
  av_opt_set(m_ctx->priv_data, "tune", "zerolatency", 0);

  const int rc = avcodec_open2(m_ctx, codec, nullptr);
  if (rc < 0) {
    setError(QStringLiteral("avcodec_open2 失败：%1").arg(ffErr(rc)));
    close();
    return false;
  }

  m_frame = av_frame_alloc();
  if (!m_frame) {
    setError(QStringLiteral("av_frame_alloc 失败"));
    close();
    return false;
  }
  m_frame->format = m_ctx->pix_fmt;
  m_frame->width = m_ctx->width;
  m_frame->height = m_ctx->height;

  const int bufRc = av_frame_get_buffer(m_frame, 32);
  if (bufRc < 0) {
    setError(QStringLiteral("av_frame_get_buffer 失败：%1").arg(ffErr(bufRc)));
    close();
    return false;
  }

  m_sws = sws_getContext(m_ctx->width,
                         m_ctx->height,
                         AV_PIX_FMT_BGRA,
                         m_ctx->width,
                         m_ctx->height,
                         AV_PIX_FMT_YUV420P,
                         SWS_FAST_BILINEAR,
                         nullptr,
                         nullptr,
                         nullptr);
  if (!m_sws) {
    setError(QStringLiteral("sws_getContext 失败"));
    close();
    return false;
  }

  m_ready = true;
  return true;
}

void FfmpegH264Encoder::close() {
  m_ready = false;
  if (m_sws) {
    sws_freeContext(m_sws);
    m_sws = nullptr;
  }
  if (m_frame) {
    av_frame_free(&m_frame);
    m_frame = nullptr;
  }
  if (m_ctx) {
    avcodec_free_context(&m_ctx);
    m_ctx = nullptr;
  }
}

QVector<FfmpegH264Encoder::Packet> FfmpegH264Encoder::encode(const QImage& argb32Frame, qint64 ptsMs) {
  QVector<Packet> out;
  if (!m_ready || !m_ctx || !m_frame || !m_sws) {
    return out;
  }
  if (argb32Frame.isNull() || argb32Frame.size() != m_size) {
    return out;
  }

  const int writableRc = av_frame_make_writable(m_frame);
  if (writableRc < 0) {
    setError(QStringLiteral("av_frame_make_writable 失败：%1").arg(ffErr(writableRc)));
    return out;
  }

  const uint8_t* srcSlice[1] = {reinterpret_cast<const uint8_t*>(argb32Frame.constBits())};
  int srcStride[1] = {static_cast<int>(argb32Frame.bytesPerLine())};
  sws_scale(m_sws, srcSlice, srcStride, 0, m_ctx->height, m_frame->data, m_frame->linesize);

  m_frame->pts = m_frameIndex++;

  const int sendRc = avcodec_send_frame(m_ctx, m_frame);
  if (sendRc < 0) {
    setError(QStringLiteral("avcodec_send_frame 失败：%1").arg(ffErr(sendRc)));
    return out;
  }

  while (true) {
    AVPacket* pkt = av_packet_alloc();
    if (!pkt) {
      break;
    }
    const int recvRc = avcodec_receive_packet(m_ctx, pkt);
    if (recvRc == AVERROR(EAGAIN) || recvRc == AVERROR_EOF) {
      av_packet_free(&pkt);
      break;
    }
    if (recvRc < 0) {
      setError(QStringLiteral("avcodec_receive_packet 失败：%1").arg(ffErr(recvRc)));
      av_packet_free(&pkt);
      break;
    }

    Packet p;
    p.data = QByteArray(reinterpret_cast<const char*>(pkt->data), pkt->size);
    p.keyFrame = (pkt->flags & AV_PKT_FLAG_KEY) != 0;
    p.ptsMs = ptsMs;
    out.push_back(std::move(p));
    av_packet_free(&pkt);
  }

  return out;
}

