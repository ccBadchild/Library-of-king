#include "ffmpeg_h264_encoder.h"

#include <QString>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/error.h>
#include <libswscale/swscale.h>
}

#include <cstring>

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

QString FfmpegH264Encoder::backendDescription() const {
  return m_backendDesc;
}

void FfmpegH264Encoder::setError(const QString& text) {
  m_error = text;
}

void FfmpegH264Encoder::applyCodecOptions(AVCodecContext* ctx, const char* codecName) {
  if (!ctx || !codecName || !ctx->priv_data) {
    return;
  }

  if (std::strstr(codecName, "nvenc")) {
    // FFmpeg NVENC：低延迟 CBR（preset 档位依 FFmpeg/NVIDIA 版本略有差异）
    av_opt_set(ctx->priv_data, "preset", "p4", 0);
    av_opt_set(ctx->priv_data, "tune", "ull", 0);
    av_opt_set(ctx->priv_data, "rc", "cbr", 0);
    av_opt_set(ctx->priv_data, "delay", "0", 0);
    av_opt_set(ctx->priv_data, "bf", "0", 0);
  } else if (std::strstr(codecName, "amf")) {
    av_opt_set(ctx->priv_data, "usage", "lowlatency", 0);
    av_opt_set(ctx->priv_data, "quality", "speed", 0);
    av_opt_set(ctx->priv_data, "preanalysis", "0", 0);
  } else if (std::strstr(codecName, "qsv")) {
    av_opt_set(ctx->priv_data, "preset", "veryfast", 0);
    av_opt_set(ctx->priv_data, "look_ahead", "0", 0);
    av_opt_set(ctx->priv_data, "async_depth", "1", 0);
  } else if (std::strstr(codecName, "libx264")) {
    av_opt_set(ctx->priv_data, "preset", "ultrafast", 0);
    av_opt_set(ctx->priv_data, "tune", "zerolatency", 0);
  }
}

bool FfmpegH264Encoder::tryOpenCodec(const AVCodec* codec,
                                     const QString& backendLabel,
                                     const QSize& size,
                                     int fps,
                                     int kbps) {
  AVCodecContext* ctx = avcodec_alloc_context3(codec);
  if (!ctx) {
    setError(QStringLiteral("avcodec_alloc_context3 失败"));
    return false;
  }

  ctx->width = size.width();
  ctx->height = size.height();
  ctx->pix_fmt = AV_PIX_FMT_YUV420P;
  ctx->time_base = AVRational{1, fps};
  ctx->framerate = AVRational{fps, 1};
  ctx->gop_size = fps;
  ctx->max_b_frames = 0;
  ctx->bit_rate = bitrateFromKbps(kbps);
  ctx->thread_count = 1;

  applyCodecOptions(ctx, codec->name);

  const int openRc = avcodec_open2(ctx, codec, nullptr);
  if (openRc < 0) {
    setError(QStringLiteral("avcodec_open2(%1) 失败：%2").arg(backendLabel).arg(ffErr(openRc)));
    avcodec_free_context(&ctx);
    return false;
  }

  AVFrame* frame = av_frame_alloc();
  if (!frame) {
    setError(QStringLiteral("av_frame_alloc 失败"));
    avcodec_free_context(&ctx);
    return false;
  }
  frame->format = ctx->pix_fmt;
  frame->width = ctx->width;
  frame->height = ctx->height;

  const int bufRc = av_frame_get_buffer(frame, 32);
  if (bufRc < 0) {
    setError(QStringLiteral("av_frame_get_buffer 失败：%1").arg(ffErr(bufRc)));
    av_frame_free(&frame);
    avcodec_free_context(&ctx);
    return false;
  }

  SwsContext* sws = sws_getContext(ctx->width,
                                     ctx->height,
                                     AV_PIX_FMT_BGRA,
                                     ctx->width,
                                     ctx->height,
                                     static_cast<AVPixelFormat>(ctx->pix_fmt),
                                     SWS_FAST_BILINEAR,
                                     nullptr,
                                     nullptr,
                                     nullptr);
  if (!sws) {
    setError(QStringLiteral("sws_getContext 失败"));
    av_frame_free(&frame);
    avcodec_free_context(&ctx);
    return false;
  }

  m_ctx = ctx;
  m_frame = frame;
  m_sws = sws;
  m_backendDesc = backendLabel;
  m_ready = true;
  m_error.clear();
  return true;
}

bool FfmpegH264Encoder::open(const QSize& size, int fps, int kbps) {
  close();
  m_error.clear();
  m_size = size;
  m_fps = qMax(1, fps);
  m_frameIndex = 0;

  static const struct {
    const char* name;
    const char* label;
  } kHwEncoders[] = {
      {"h264_nvenc", "NVENC"},
      {"h264_amf", "AMF"},
      {"h264_qsv", "QSV"},
  };

  for (const auto& h : kHwEncoders) {
    const AVCodec* codec = avcodec_find_encoder_by_name(h.name);
    if (!codec) {
      continue;
    }
    if (tryOpenCodec(codec, QString::fromUtf8(h.label), size, m_fps, kbps)) {
      return true;
    }
    // tryOpenCodec 失败时已 setError；清理解码器状态，便于尝试下一种
    close();
    m_error.clear();
  }

  const AVCodec* sw = avcodec_find_encoder_by_name("libx264");
  if (!sw) {
    sw = avcodec_find_encoder(AV_CODEC_ID_H264);
  }
  if (!sw) {
    setError(QStringLiteral("找不到可用的 H.264 软件编码器"));
    return false;
  }

  QString swLabel = QStringLiteral("software");
  if (std::strstr(sw->name, "libx264")) {
    swLabel = QStringLiteral("software (libx264)");
  } else {
    swLabel = QStringLiteral("software (%1)").arg(QString::fromUtf8(sw->name));
  }

  return tryOpenCodec(sw, swLabel, size, m_fps, kbps);
}

void FfmpegH264Encoder::close() {
  m_ready = false;
  m_backendDesc.clear();
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
