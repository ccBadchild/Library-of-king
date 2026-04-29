#pragma once

#include <QByteArray>
#include <QDataStream>
#include <QImage>
#include <QRect>
#include <QString>
#include <QVector>
#include <QMetaType>

namespace rdqt {

enum class MessageType : quint32 {
  Hello = 1,
  HelloAck = 2,
  FullFrame = 3,
  PatchFrame = 4,
  InputEvent = 5,
  Ping = 6,
  Pong = 7,
  VideoFrame = 8
};

enum class QualityPreset : quint8 {
  Low = 0,
  Medium = 1,
  High = 2
};

enum class VideoCodec : quint8 {
  Auto = 0,
  Jpeg = 1,
  H264 = 2
};

struct PatchBlock {
  QRect rect;
  QByteArray jpegData;
};

enum class InputEventType : quint8 {
  MouseMove = 1,
  MouseDown = 2,
  MouseUp = 3,
  MouseWheel = 4,
  KeyDown = 5,
  KeyUp = 6
};

struct RemoteInputEvent {
  InputEventType type = InputEventType::MouseMove;
  qint32 x = 0;
  qint32 y = 0;
  qint32 delta = 0;
  qint32 key = 0;
  qint32 buttons = 0;
  qint32 modifiers = 0;
};

inline int jpegQualityFromPreset(QualityPreset preset) {
  switch (preset) {
    case QualityPreset::Low:
      return 35;
    case QualityPreset::Medium:
      return 60;
    case QualityPreset::High:
      return 82;
    default:
      return 60;
  }
}

inline QSize scaleSizeFromPreset(const QSize& src, QualityPreset preset) {
  switch (preset) {
    case QualityPreset::Low:
      return QSize(src.width() * 2 / 5, src.height() * 2 / 5);
    case QualityPreset::Medium:
      return QSize(src.width() * 2 / 3, src.height() * 2 / 3);
    case QualityPreset::High:
      return src;
    default:
      return src;
  }
}

// 统一消息封装：4字节类型 + 4字节负载长度 + 负载内容。
inline QByteArray packMessage(MessageType type, const QByteArray& payload) {
  QByteArray out;
  QDataStream ds(&out, QIODevice::WriteOnly);
  ds.setVersion(QDataStream::Qt_5_15);
  ds << static_cast<quint32>(type);
  ds << static_cast<quint32>(payload.size());
  out.append(payload);
  return out;
}

inline QByteArray makeHelloPayload(const QString& verifyCode, QualityPreset preset, VideoCodec codec) {
  QByteArray payload;
  QDataStream ds(&payload, QIODevice::WriteOnly);
  ds.setVersion(QDataStream::Qt_5_15);
  ds << verifyCode;
  ds << static_cast<quint8>(preset);
  ds << static_cast<quint8>(codec);
  return payload;
}

inline QByteArray makeHelloAckPayload(bool ok, const QString& reason, VideoCodec codec = VideoCodec::Jpeg) {
  QByteArray payload;
  QDataStream ds(&payload, QIODevice::WriteOnly);
  ds.setVersion(QDataStream::Qt_5_15);
  ds << static_cast<quint8>(ok ? 1 : 0);
  ds << reason;
  ds << static_cast<quint8>(codec);
  return payload;
}

inline QByteArray makeFullFramePayload(const QSize& size, const QByteArray& jpegData) {
  QByteArray payload;
  QDataStream ds(&payload, QIODevice::WriteOnly);
  ds.setVersion(QDataStream::Qt_5_15);
  ds << static_cast<quint32>(size.width());
  ds << static_cast<quint32>(size.height());
  ds << jpegData;
  return payload;
}

inline QByteArray makePatchPayload(const QVector<PatchBlock>& patches) {
  QByteArray payload;
  QDataStream ds(&payload, QIODevice::WriteOnly);
  ds.setVersion(QDataStream::Qt_5_15);
  ds << static_cast<quint32>(patches.size());
  for (const PatchBlock& p : patches) {
    ds << static_cast<qint32>(p.rect.x());
    ds << static_cast<qint32>(p.rect.y());
    ds << static_cast<qint32>(p.rect.width());
    ds << static_cast<qint32>(p.rect.height());
    ds << p.jpegData;
  }
  return payload;
}

inline QByteArray makeInputEventPayload(const RemoteInputEvent& event) {
  QByteArray payload;
  QDataStream ds(&payload, QIODevice::WriteOnly);
  ds.setVersion(QDataStream::Qt_5_15);
  ds << static_cast<quint8>(event.type);
  ds << event.x << event.y << event.delta << event.key << event.buttons << event.modifiers;
  return payload;
}

inline QByteArray makePingPayload(qint64 clientTsMs) {
  QByteArray payload;
  QDataStream ds(&payload, QIODevice::WriteOnly);
  ds.setVersion(QDataStream::Qt_5_15);
  ds << clientTsMs;
  return payload;
}

inline QByteArray makeVideoFramePayload(const QSize& size, bool keyFrame, qint64 ptsMs, const QByteArray& data) {
  QByteArray payload;
  QDataStream ds(&payload, QIODevice::WriteOnly);
  ds.setVersion(QDataStream::Qt_5_15);
  ds << static_cast<quint32>(size.width());
  ds << static_cast<quint32>(size.height());
  ds << static_cast<quint8>(keyFrame ? 1 : 0);
  ds << static_cast<qint64>(ptsMs);
  ds << data;
  return payload;
}

} // namespace rdqt

Q_DECLARE_METATYPE(rdqt::QualityPreset)
Q_DECLARE_METATYPE(rdqt::VideoCodec)
Q_DECLARE_METATYPE(rdqt::PatchBlock)
Q_DECLARE_METATYPE(QVector<rdqt::PatchBlock>)
Q_DECLARE_METATYPE(rdqt::RemoteInputEvent)
