#pragma once

#include <QString>

namespace applog {

enum class Role {
  Server,
  Client
};

void write(Role role, const QString& message);
QString filePath(Role role);

} // namespace applog
