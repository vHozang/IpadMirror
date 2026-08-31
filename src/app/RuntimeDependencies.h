#pragma once

#include <QStringList>

namespace padmirror::app {

class RuntimeDependencies final {
public:
    static void configureEnvironment();
    [[nodiscard]] static QStringList missingComponents();
    [[nodiscard]] static QStringList featureReport();
};

} // namespace padmirror::app
