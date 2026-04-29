#include "tcp_file_server_worker.h"

#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>

TcpFileServerWorker::TcpFileServerWorker(QObject* parent) : QObject(parent) {
}

void TcpFileServerWorker::startListen(quint16 port, const QString& verifyCode) {
  // 文件服务单独监听 control_port + 1，和实时视频/输入通道完全隔离，
  // 这样大文件传输不会把主控制链路挤占到卡顿。
  ensureServerCreated();
  stopListen();
  m_verifyCode = verifyCode;
  if (!m_server || !m_server->listen(QHostAddress::Any, port)) {
    emit logMessage(QStringLiteral("文件通道监听失败：%1").arg(m_server ? m_server->errorString() : QStringLiteral("服务未初始化")));
    return;
  }
  emit logMessage(QStringLiteral("文件通道已启动，监听端口：%1").arg(port));
}

void TcpFileServerWorker::stopListen() {
  resetClient();
  if (m_server && m_server->isListening()) {
    m_server->close();
  }
}

void TcpFileServerWorker::onNewConnection() {
  if (!m_server) {
    return;
  }
  if (m_client) {
    QTcpSocket* old = m_server->nextPendingConnection();
    old->disconnectFromHost();
    old->deleteLater();
    return;
  }
  m_client = m_server->nextPendingConnection();
  m_clientVerified = false;
  m_readBuffer.clear();
  connect(m_client, &QTcpSocket::readyRead, this, &TcpFileServerWorker::onSocketReadyRead);
  connect(m_client, &QTcpSocket::disconnected, this, &TcpFileServerWorker::onSocketDisconnected);
  emit logMessage(QStringLiteral("文件通道客户端已连接，等待校验..."));
}

void TcpFileServerWorker::onSocketReadyRead() {
  if (!m_client) {
    return;
  }
  // 服务端同样按字节流协议拆包；在 m_clientVerified 为 false 时，只允许收到 FileHello。
  m_readBuffer.append(m_client->readAll());
  while (true) {
    QByteArray body;
    rdqt::MessageType type{};
    if (!tryParseMessage(body, type)) {
      break;
    }

    if (!m_clientVerified) {
      if (type != rdqt::MessageType::FileHello) {
        m_client->disconnectFromHost();
        return;
      }
      QDataStream ds(body);
      ds.setVersion(QDataStream::Qt_5_15);
      QString code;
      ds >> code;
      const bool ok = (code == m_verifyCode);
      const QString reason = ok ? QStringLiteral("文件通道校验成功") : QStringLiteral("文件通道验证码错误");
      m_client->write(rdqt::packMessage(rdqt::MessageType::FileHelloAck, rdqt::makeFileHelloAckPayload(ok, reason)));
      if (!ok) {
        emit logMessage(QStringLiteral("文件通道校验失败"));
        m_client->disconnectFromHost();
        return;
      }
      m_clientVerified = true;
      emit logMessage(QStringLiteral("文件通道校验通过"));
      continue;
    }

    if (type == rdqt::MessageType::FsListRootsReq) {
      m_client->write(rdqt::packMessage(rdqt::MessageType::FsListRootsRsp, rdqt::makeFileEntriesPayload(QString(), listRoots())));
      continue;
    }

    if (type == rdqt::MessageType::FsListDirReq) {
      QDataStream ds(body);
      ds.setVersion(QDataStream::Qt_5_15);
      QString path;
      ds >> path;
      m_client->write(rdqt::packMessage(rdqt::MessageType::FsListDirRsp, rdqt::makeFileEntriesPayload(path, listDirectory(path))));
      continue;
    }

    if (type == rdqt::MessageType::FsReadFileReq) {
      QDataStream ds(body);
      ds.setVersion(QDataStream::Qt_5_15);
      QString path;
      ds >> path;
      QString error;
      const QByteArray data = readFileData(path, &error);
      const bool ok = error.isEmpty();
      m_client->write(rdqt::packMessage(
          rdqt::MessageType::FsReadFileRsp,
          rdqt::makeReadFileResponsePayload(path, ok, ok ? QStringLiteral("读取成功") : error, data)));
      continue;
    }

    if (type == rdqt::MessageType::FsWriteFileReq) {
      QDataStream ds(body);
      ds.setVersion(QDataStream::Qt_5_15);
      QString targetPath;
      QByteArray data;
      ds >> targetPath >> data;
      QString error;
      const bool ok = writeFileData(targetPath, data, &error);
      m_client->write(rdqt::packMessage(
          rdqt::MessageType::FsWriteFileRsp,
          rdqt::makeWriteFileResponsePayload(targetPath, ok, ok ? QStringLiteral("写入成功") : error)));
      continue;
    }

    if (type == rdqt::MessageType::FsReadChunkReq) {
      // 下载场景：客户端给出 path + offset + maxBytes，服务端按偏移读取一个分片返回。
      QDataStream ds(body);
      ds.setVersion(QDataStream::Qt_5_15);
      QString path;
      qint64 offset = 0;
      qint32 maxBytes = 0;
      ds >> path >> offset >> maxBytes;
      QString error;
      qint64 totalSize = 0;
      const QByteArray data = readFileChunk(path, offset, maxBytes, &totalSize, &error);
      const bool ok = error.isEmpty();
      const bool done = ok ? (offset + data.size() >= totalSize) : true;
      m_client->write(rdqt::packMessage(
          rdqt::MessageType::FsReadChunkRsp,
          rdqt::makeReadChunkResponsePayload(path, totalSize, offset, done, ok, ok ? QStringLiteral("读取分片成功") : error, data)));
      continue;
    }

    if (type == rdqt::MessageType::FsWriteChunkReq) {
      // 上传场景：客户端把文件总长、当前偏移和分片数据一起发来，
      // 服务端按 offset seek 写入，这样断线重连后可以继续续传。
      QDataStream ds(body);
      ds.setVersion(QDataStream::Qt_5_15);
      QString targetPath;
      qint64 totalSize = 0;
      qint64 offset = 0;
      QByteArray data;
      ds >> targetPath >> totalSize >> offset >> data;
      QString error;
      const bool ok = writeFileChunk(targetPath, totalSize, offset, data, &error);
      const bool done = ok ? (offset + data.size() >= totalSize) : true;
      m_client->write(rdqt::packMessage(
          rdqt::MessageType::FsWriteChunkRsp,
          rdqt::makeWriteChunkResponsePayload(
              targetPath, totalSize, offset, static_cast<qint32>(data.size()), done, ok, ok ? QStringLiteral("写入分片成功") : error)));
      continue;
    }

    if (type == rdqt::MessageType::FsDeletePathReq) {
      QDataStream ds(body);
      ds.setVersion(QDataStream::Qt_5_15);
      QString path;
      quint8 recursive = 0;
      ds >> path >> recursive;
      QString error;
      const bool ok = deletePath(path, recursive == 1, &error);
      m_client->write(rdqt::packMessage(
          rdqt::MessageType::FsDeletePathRsp,
          rdqt::makeDeletePathResponsePayload(path, ok, ok ? QStringLiteral("删除成功") : error)));
      continue;
    }
  }
}

void TcpFileServerWorker::onSocketDisconnected() {
  emit logMessage(QStringLiteral("文件通道客户端已断开"));
  resetClient();
}

bool TcpFileServerWorker::tryParseMessage(QByteArray& body, rdqt::MessageType& typeOut) {
  if (m_readBuffer.size() < 8) {
    return false;
  }
  QDataStream head(m_readBuffer.left(8));
  head.setVersion(QDataStream::Qt_5_15);
  quint32 type = 0;
  quint32 len = 0;
  head >> type >> len;
  if (m_readBuffer.size() < 8 + static_cast<int>(len)) {
    return false;
  }
  body = m_readBuffer.mid(8, static_cast<int>(len));
  m_readBuffer.remove(0, 8 + static_cast<int>(len));
  typeOut = static_cast<rdqt::MessageType>(type);
  return true;
}

void TcpFileServerWorker::ensureServerCreated() {
  if (m_server) {
    return;
  }
  m_server = new QTcpServer(this);
  connect(m_server, &QTcpServer::newConnection, this, &TcpFileServerWorker::onNewConnection);
}

void TcpFileServerWorker::resetClient() {
  if (m_client) {
    m_client->deleteLater();
    m_client = nullptr;
  }
  m_clientVerified = false;
  m_readBuffer.clear();
}

QVector<rdqt::FileEntry> TcpFileServerWorker::listRoots() const {
  // 盘符根目录浏览是后续所有远程操作的入口，因此在 Windows 下直接枚举 drives()。
  QVector<rdqt::FileEntry> entries;
  const auto drives = QDir::drives();
  entries.reserve(drives.size());
  for (const QFileInfo& drive : drives) {
    rdqt::FileEntry entry;
    entry.name = QDir::toNativeSeparators(drive.absoluteFilePath());
    entry.path = drive.absoluteFilePath();
    entry.isDir = true;
    entries.push_back(entry);
  }
  return entries;
}

QVector<rdqt::FileEntry> TcpFileServerWorker::listDirectory(const QString& path) const {
  QVector<rdqt::FileEntry> entries;
  QDir dir(path);
  if (!dir.exists()) {
    return entries;
  }
  const QFileInfoList items =
      dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                        QDir::DirsFirst | QDir::IgnoreCase | QDir::Name);
  entries.reserve(items.size());
  for (const QFileInfo& info : items) {
    rdqt::FileEntry entry;
    entry.name = info.fileName();
    entry.path = info.absoluteFilePath();
    entry.isDir = info.isDir();
    entry.size = entry.isDir ? -1 : info.size();
    entries.push_back(entry);
  }
  return entries;
}

QByteArray TcpFileServerWorker::readFileData(const QString& path, QString* error) const {
  QFile file(path);
  if (!file.exists()) {
    if (error) {
      *error = QStringLiteral("远程文件不存在");
    }
    return {};
  }
  if (!file.open(QIODevice::ReadOnly)) {
    if (error) {
      *error = QStringLiteral("远程文件打开失败：%1").arg(file.errorString());
    }
    return {};
  }
  return file.readAll();
}

bool TcpFileServerWorker::writeFileData(const QString& path, const QByteArray& data, QString* error) const {
  QFileInfo info(path);
  QDir dir = info.dir();
  if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
    if (error) {
      *error = QStringLiteral("远程目录创建失败");
    }
    return false;
  }
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly)) {
    if (error) {
      *error = QStringLiteral("远程文件写入失败：%1").arg(file.errorString());
    }
    return false;
  }
  if (file.write(data) != data.size()) {
    if (error) {
      *error = QStringLiteral("远程文件写入不完整");
    }
    return false;
  }
  file.close();
  return true;
}

QByteArray TcpFileServerWorker::readFileChunk(const QString& path,
                                              qint64 offset,
                                              qint32 maxBytes,
                                              qint64* totalSize,
                                              QString* error) const {
  // 分片读取而不是 readAll()，避免大文件一次性占满内存。
  QFile file(path);
  if (!file.exists()) {
    if (error) {
      *error = QStringLiteral("远程文件不存在");
    }
    return {};
  }
  if (!file.open(QIODevice::ReadOnly)) {
    if (error) {
      *error = QStringLiteral("远程文件打开失败：%1").arg(file.errorString());
    }
    return {};
  }
  if (totalSize) {
    *totalSize = file.size();
  }
  if (offset < 0 || offset > file.size()) {
    if (error) {
      *error = QStringLiteral("远程读取偏移无效");
    }
    return {};
  }
  file.seek(offset);
  return file.read(maxBytes > 0 ? maxBytes : 0);
}

bool TcpFileServerWorker::writeFileChunk(const QString& path,
                                         qint64 totalSize,
                                         qint64 offset,
                                         const QByteArray& data,
                                         QString* error) const {
  // 分片写入的关键点：
  // 1. offset=0 时清空旧文件，确保覆盖上传不会保留旧尾巴
  // 2. 每次都 seek 到目标偏移，这样重连后从任意断点继续写仍然成立
  // 3. 最后一片到达时再 resize(totalSize)，保证文件长度最终正确
  QFileInfo info(path);
  QDir dir = info.dir();
  if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
    if (error) {
      *error = QStringLiteral("远程目录创建失败");
    }
    return false;
  }
  QFile file(path);
  if (!file.open(QIODevice::ReadWrite)) {
    if (!file.open(QIODevice::WriteOnly)) {
      if (error) {
        *error = QStringLiteral("远程文件写入失败：%1").arg(file.errorString());
      }
      return false;
    }
  }
  if (offset == 0) {
    file.resize(0);
  }
  if (!file.seek(offset)) {
    if (error) {
      *error = QStringLiteral("远程文件定位失败");
    }
    return false;
  }
  if (file.write(data) != data.size()) {
    if (error) {
      *error = QStringLiteral("远程文件分片写入失败");
    }
    return false;
  }
  if (offset + data.size() >= totalSize) {
    file.resize(totalSize);
  }
  file.close();
  return true;
}

bool TcpFileServerWorker::deletePath(const QString& path, bool recursive, QString* error) const {
  if (path.isEmpty()) {
    if (error) {
      *error = QStringLiteral("删除路径为空");
    }
    return false;
  }
  QFileInfo info(path);
  if (!info.exists()) {
    if (error) {
      *error = QStringLiteral("目标不存在");
    }
    return false;
  }
  if (info.isDir()) {
    QDir dir(path);
    bool ok = false;
    if (recursive) {
      ok = dir.removeRecursively();
    } else {
      QDir parentDir = info.dir();
      ok = parentDir.rmdir(info.fileName());
    }
    if (!ok && error) {
      *error = recursive ? QStringLiteral("远程目录递归删除失败") : QStringLiteral("远程目录非空，删除失败");
    }
    return ok;
  }
  const bool ok = QFile::remove(path);
  if (!ok && error) {
    *error = QStringLiteral("远程文件删除失败");
  }
  return ok;
}
