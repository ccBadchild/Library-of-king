#include "app_logger.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMutex>
#include <QMutexLocker>
#include <QTextStream>

namespace applog {
namespace {

QMutex g_logMutex;

QString roleName(Role role) {
  return role == Role::Server ? QStringLiteral("server") : QStringLiteral("client");
}

} // namespace

QString filePath(Role role) {
  const QString logDir = QCoreApplication::applicationDirPath() + "/logs";
  QDir dir;
  if (!dir.exists(logDir)) {
    dir.mkpath(logDir);
  }
  return logDir + "/" + roleName(role) + ".log";
}

void write(Role role, const QString& message) {
  QMutexLocker locker(&g_logMutex);
  QFile file(filePath(role));
  if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
    return;
  }
  QTextStream out(&file);
  out.setCodec("UTF-8");
  const QString ts = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz");
  out << "[" << ts << "] " << message << "\n";
  out.flush();
}

} // namespace applog
