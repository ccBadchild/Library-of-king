#pragma once

/**
 * @file app_logger.h
 * @brief 简单文件日志：按角色（服务端/客户端）写入 exe 同级目录下的 logs\\*.log，线程安全。
 */

#include <QString>

namespace applog {

/** 日志归属：服务端进程与客户端进程分别写入不同文件名，便于对照排查。 */
enum class Role {
  Server,
  Client
};

/** 追加一行 UTF-8 文本日志（带时间戳）；失败时静默忽略（如无写权限）。 */
void write(Role role, const QString& message);
/** 返回当前角色对应的日志文件完整路径（必要时创建 logs 目录）。 */
QString filePath(Role role);

} // namespace applog
