#pragma once

#include <QString>

namespace padmirror::app {

void installLogging();
[[nodiscard]] QString logDirectoryPath();

} // namespace padmirror::app
