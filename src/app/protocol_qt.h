#pragma once

/**
 * @file protocol_qt.h
 * @brief 自定义二进制协议（Qt/QDataStream）：消息类型、画质与编码枚举、握手与负载封装函数。
 *
 * 每条 TCP 消息格式固定为：32bit MessageType + 32bit payload 字节长度 + payload（见 packMessage）。
 * 跨线程 queued signal/slot 传递的结构体需在 main.cpp 中 qRegisterMetaType。
 */

#include <QByteArray>
#include <QDataStream>
#include <QImage>
#include <QRect>
#include <QString>
#include <QVector>
#include <QMetaType>

namespace rdqt {

/** 应用层消息类型（含握手、JPEG 整帧/补丁、输入事件、Ping/Pong、H.264 裸包）。 */
enum class MessageType : quint32 {
  Hello = 1,
  HelloAck = 2,
  FullFrame = 3,
  PatchFrame = 4,
  InputEvent = 5,
  Ping = 6,
  Pong = 7,
  VideoFrame = 8,
  FileHello = 20,
  FileHelloAck = 21,
  FsListRootsReq = 22,
  FsListRootsRsp = 23,
  FsListDirReq = 24,
  FsListDirRsp = 25,
  FsReadFileReq = 26,
  FsReadFileRsp = 27,
  FsWriteFileReq = 28,
  FsWriteFileRsp = 29,
  FsReadChunkReq = 30,
  FsReadChunkRsp = 31,
  FsWriteChunkReq = 32,
  FsWriteChunkRsp = 33,
  FsDeletePathReq = 34,
  FsDeletePathRsp = 35
};

/** 清晰度档位：影响服务端缩放分辨率与 JPEG 质量；Auto 编码模式下还与协商 H264/JPEG 有关。 */
enum class QualityPreset : quint8 {
  Low = 0,
  Medium = 1,
  High = 2
};

/** 视频编码偏好：Auto 由服务端结合 QualityPreset 决定实际 JPEG 或 H.264。 */
enum class VideoCodec : quint8 {
  Auto = 0,
  Jpeg = 1,
  H264 = 2
};

/** JPEG 补丁帧中的一块矩形区域及其 JPEG 压缩字节流。 */
struct PatchBlock {
  QRect rect;
  QByteArray jpegData;
};

/** 远端输入事件类型（键鼠）；坐标在 PatchFrame 路径外常与桌面 SendInput 约定一致。 */
enum class InputEventType : quint8 {
  MouseMove = 1,
  MouseDown = 2,
  MouseUp = 3,
  MouseWheel = 4,
  KeyDown = 5,
  KeyUp = 6
};

/** 单一远程输入事件载荷（与 makeInputEventPayload / 服务端解析顺序一致）。 */
struct RemoteInputEvent {
  InputEventType type = InputEventType::MouseMove;
  qint32 x = 0;
  qint32 y = 0;
  qint32 delta = 0;
  qint32 key = 0;
  qint32 buttons = 0;
  qint32 modifiers = 0;
};

/** 远程文件浏览目录项：名称、绝对路径、目录标记与大小。 */
struct FileEntry {
  QString name;
  QString path;
  bool isDir = false;
  qint64 size = -1;
};

/** 按清晰度档位映射 JPEG 量化质量参数（数值越小体积越小）。 */
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

/** 将物理分辨率缩放到逻辑发送分辨率（减轻带宽）。 */
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

/** 统一消息封装：4 字节类型 + 4 字节负载长度 + 负载原始字节。 */
inline QByteArray packMessage(MessageType type, const QByteArray& payload) {
  QByteArray out;
  QDataStream ds(&out, QIODevice::WriteOnly);
  ds.setVersion(QDataStream::Qt_5_15);
  ds << static_cast<quint32>(type);
  ds << static_cast<quint32>(payload.size());
  out.append(payload);
  return out;
}

/** Hello 负载：验证码字符串 + 清晰度 + 客户端请求的编码枚举。 */
inline QByteArray makeHelloPayload(const QString& verifyCode, QualityPreset preset, VideoCodec codec) {
  QByteArray payload;
  QDataStream ds(&payload, QIODevice::WriteOnly);
  ds.setVersion(QDataStream::Qt_5_15);
  ds << verifyCode;
  ds << static_cast<quint8>(preset);
  ds << static_cast<quint8>(codec);
  return payload;
}

/** HelloAck：是否成功 + 文本原因 + 服务端选定编码（JPEG 或 H264）。 */
inline QByteArray makeHelloAckPayload(bool ok, const QString& reason, VideoCodec codec = VideoCodec::Jpeg) {
  QByteArray payload;
  QDataStream ds(&payload, QIODevice::WriteOnly);
  ds.setVersion(QDataStream::Qt_5_15);
  ds << static_cast<quint8>(ok ? 1 : 0);
  ds << reason;
  ds << static_cast<quint8>(codec);
  return payload;
}

/** 整帧 JPEG：宽高 + jpegData。 */
inline QByteArray makeFullFramePayload(const QSize& size, const QByteArray& jpegData) {
  QByteArray payload;
  QDataStream ds(&payload, QIODevice::WriteOnly);
  ds.setVersion(QDataStream::Qt_5_15);
  ds << static_cast<quint32>(size.width());
  ds << static_cast<quint32>(size.height());
  ds << jpegData;
  return payload;
}

/** 多块补丁：块数量 + 每块 x,y,w,h + jpegData。 */
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

/** 远程输入序列化（字段顺序须与 TcpServerWorker::applyRemoteInput 读取一致）。 */
inline QByteArray makeInputEventPayload(const RemoteInputEvent& event) {
  QByteArray payload;
  QDataStream ds(&payload, QIODevice::WriteOnly);
  ds.setVersion(QDataStream::Qt_5_15);
  ds << static_cast<quint8>(event.type);
  ds << event.x << event.y << event.delta << event.key << event.buttons << event.modifiers;
  return payload;
}

/** Ping：客户端毫秒时间戳，服务端原样写入 Pong 负载用于 RTT。 */
inline QByteArray makePingPayload(qint64 clientTsMs) {
  QByteArray payload;
  QDataStream ds(&payload, QIODevice::WriteOnly);
  ds.setVersion(QDataStream::Qt_5_15);
  ds << clientTsMs;
  return payload;
}

/** H.264 一帧负载：逻辑宽高 + 是否关键帧 + PTS（毫秒）+ Annex B 字节。 */
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

inline QByteArray makeFileHelloPayload(const QString& verifyCode) {
  QByteArray payload;
  QDataStream ds(&payload, QIODevice::WriteOnly);
  ds.setVersion(QDataStream::Qt_5_15);
  ds << verifyCode;
  return payload;
}

inline QByteArray makeFileHelloAckPayload(bool ok, const QString& reason) {
  QByteArray payload;
  QDataStream ds(&payload, QIODevice::WriteOnly);
  ds.setVersion(QDataStream::Qt_5_15);
  ds << static_cast<quint8>(ok ? 1 : 0);
  ds << reason;
  return payload;
}

inline QByteArray makeListDirPayload(const QString& path) {
  QByteArray payload;
  QDataStream ds(&payload, QIODevice::WriteOnly);
  ds.setVersion(QDataStream::Qt_5_15);
  ds << path;
  return payload;
}

inline QByteArray makeFileEntriesPayload(const QString& basePath, const QVector<FileEntry>& entries) {
  QByteArray payload;
  QDataStream ds(&payload, QIODevice::WriteOnly);
  ds.setVersion(QDataStream::Qt_5_15);
  ds << basePath;
  ds << static_cast<quint32>(entries.size());
  for (const FileEntry& entry : entries) {
    ds << entry.name;
    ds << entry.path;
    ds << static_cast<quint8>(entry.isDir ? 1 : 0);
    ds << entry.size;
  }
  return payload;
}

inline QByteArray makeReadFilePayload(const QString& path) {
  QByteArray payload;
  QDataStream ds(&payload, QIODevice::WriteOnly);
  ds.setVersion(QDataStream::Qt_5_15);
  ds << path;
  return payload;
}

inline QByteArray makeReadFileResponsePayload(const QString& path, bool ok, const QString& reason, const QByteArray& data) {
  QByteArray payload;
  QDataStream ds(&payload, QIODevice::WriteOnly);
  ds.setVersion(QDataStream::Qt_5_15);
  ds << path;
  ds << static_cast<quint8>(ok ? 1 : 0);
  ds << reason;
  ds << data;
  return payload;
}

inline QByteArray makeWriteFilePayload(const QString& targetPath, const QByteArray& data) {
  QByteArray payload;
  QDataStream ds(&payload, QIODevice::WriteOnly);
  ds.setVersion(QDataStream::Qt_5_15);
  ds << targetPath;
  ds << data;
  return payload;
}

inline QByteArray makeWriteFileResponsePayload(const QString& targetPath, bool ok, const QString& reason) {
  QByteArray payload;
  QDataStream ds(&payload, QIODevice::WriteOnly);
  ds.setVersion(QDataStream::Qt_5_15);
  ds << targetPath;
  ds << static_cast<quint8>(ok ? 1 : 0);
  ds << reason;
  return payload;
}

inline QByteArray makeReadChunkPayload(const QString& path, qint64 offset, qint32 maxBytes) {
  QByteArray payload;
  QDataStream ds(&payload, QIODevice::WriteOnly);
  ds.setVersion(QDataStream::Qt_5_15);
  ds << path << offset << maxBytes;
  return payload;
}

inline QByteArray makeReadChunkResponsePayload(const QString& path,
                                               qint64 totalSize,
                                               qint64 offset,
                                               bool done,
                                               bool ok,
                                               const QString& reason,
                                               const QByteArray& data) {
  QByteArray payload;
  QDataStream ds(&payload, QIODevice::WriteOnly);
  ds.setVersion(QDataStream::Qt_5_15);
  ds << path << totalSize << offset;
  ds << static_cast<quint8>(done ? 1 : 0);
  ds << static_cast<quint8>(ok ? 1 : 0);
  ds << reason;
  ds << data;
  return payload;
}

inline QByteArray makeWriteChunkPayload(const QString& targetPath,
                                        qint64 totalSize,
                                        qint64 offset,
                                        const QByteArray& data) {
  QByteArray payload;
  QDataStream ds(&payload, QIODevice::WriteOnly);
  ds.setVersion(QDataStream::Qt_5_15);
  ds << targetPath << totalSize << offset << data;
  return payload;
}

inline QByteArray makeWriteChunkResponsePayload(const QString& targetPath,
                                                qint64 totalSize,
                                                qint64 offset,
                                                qint32 writtenBytes,
                                                bool done,
                                                bool ok,
                                                const QString& reason) {
  QByteArray payload;
  QDataStream ds(&payload, QIODevice::WriteOnly);
  ds.setVersion(QDataStream::Qt_5_15);
  ds << targetPath << totalSize << offset << writtenBytes;
  ds << static_cast<quint8>(done ? 1 : 0);
  ds << static_cast<quint8>(ok ? 1 : 0);
  ds << reason;
  return payload;
}

inline QByteArray makeDeletePathPayload(const QString& path, bool recursive) {
  QByteArray payload;
  QDataStream ds(&payload, QIODevice::WriteOnly);
  ds.setVersion(QDataStream::Qt_5_15);
  ds << path;
  ds << static_cast<quint8>(recursive ? 1 : 0);
  return payload;
}

inline QByteArray makeDeletePathResponsePayload(const QString& path, bool ok, const QString& reason) {
  QByteArray payload;
  QDataStream ds(&payload, QIODevice::WriteOnly);
  ds.setVersion(QDataStream::Qt_5_15);
  ds << path;
  ds << static_cast<quint8>(ok ? 1 : 0);
  ds << reason;
  return payload;
}

inline bool readFileEntriesPayload(const QByteArray& body, QString* basePath, QVector<FileEntry>* entriesOut) {
  if (!basePath || !entriesOut) {
    return false;
  }
  QDataStream ds(body);
  ds.setVersion(QDataStream::Qt_5_15);
  quint32 count = 0;
  ds >> *basePath >> count;
  QVector<FileEntry> entries;
  entries.reserve(static_cast<int>(count));
  for (quint32 i = 0; i < count; ++i) {
    FileEntry entry;
    quint8 isDir = 0;
    ds >> entry.name >> entry.path >> isDir >> entry.size;
    entry.isDir = (isDir == 1);
    entries.push_back(entry);
  }
  *entriesOut = entries;
  return ds.status() == QDataStream::Ok;
}

} // namespace rdqt

Q_DECLARE_METATYPE(rdqt::QualityPreset)
Q_DECLARE_METATYPE(rdqt::VideoCodec)
Q_DECLARE_METATYPE(rdqt::PatchBlock)
Q_DECLARE_METATYPE(QVector<rdqt::PatchBlock>)
Q_DECLARE_METATYPE(rdqt::RemoteInputEvent)
Q_DECLARE_METATYPE(rdqt::FileEntry)
Q_DECLARE_METATYPE(QVector<rdqt::FileEntry>)
