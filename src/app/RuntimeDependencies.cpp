#include "app/RuntimeDependencies.h"

#include <gst/gst.h>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

#include <initializer_list>

namespace padmirror::app {
namespace {

QString prependPath(const QString& entry, const QByteArray& current) {
    if (entry.isEmpty()) {
        return QString::fromLocal8Bit(current);
    }
    const auto existing = QString::fromLocal8Bit(current);
    return existing.isEmpty() ? entry : entry + QDir::listSeparator() + existing;
}

bool hasFactory(const char* name) {
    auto* factory = gst_element_factory_find(name);
    if (!factory) {
        return false;
    }
    gst_object_unref(factory);
    return true;
}

void requireFactory(QStringList& missing, const char* label,
                    std::initializer_list<const char*> names) {
    for (const auto* name : names) {
        if (hasFactory(name)) {
            return;
        }
    }
    missing.push_back(QString::fromLatin1(label));
}

} // namespace

void RuntimeDependencies::configureEnvironment() {
#ifdef Q_OS_WIN
    const QDir applicationDir(QCoreApplication::applicationDirPath());
    const auto pluginDir = applicationDir.filePath(QStringLiteral("gstreamer-1.0"));
    if (QFileInfo::exists(pluginDir)) {
        qputenv("GST_PLUGIN_PATH_1_0", pluginDir.toLocal8Bit());
        qputenv("GST_PLUGIN_SYSTEM_PATH_1_0", pluginDir.toLocal8Bit());

        const auto cacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
        if (!cacheDir.isEmpty() && QDir().mkpath(cacheDir)) {
            const auto seed = QCoreApplication::applicationFilePath().toLower().toUtf8();
            const auto key = QCryptographicHash::hash(seed, QCryptographicHash::Sha256)
                                 .toHex().left(12);
            qputenv("GST_REGISTRY_1_0",
                    QDir(cacheDir)
                        .filePath(QStringLiteral("registry-%1.bin")
                                      .arg(QString::fromLatin1(key)))
                        .toLocal8Bit());
        }
    }

    QStringList roots = {
        applicationDir.absolutePath(),
        qEnvironmentVariable("GSTREAMER_ROOT_X86_64"),
        qEnvironmentVariable("GSTREAMER_1_0_ROOT_MSVC_X86_64"),
        QStringLiteral("C:/gstreamer/1.0/msvc_x86_64"),
    };
    roots.removeAll(QString());

    auto path = QString::fromLocal8Bit(qgetenv("PATH"));
    QString scanner;
    for (const auto& root : roots) {
        const QDir directory(root);
        const auto bin = directory.filePath(QStringLiteral("bin"));
        if (QFileInfo::exists(bin)) {
            path = prependPath(QDir::toNativeSeparators(bin), path.toLocal8Bit());
        }
        const auto candidate = directory.filePath(
            QStringLiteral("libexec/gstreamer-1.0/gst-plugin-scanner.exe"));
        if (scanner.isEmpty() && QFileInfo::exists(candidate)) {
            scanner = candidate;
        }
    }
    qputenv("PATH", path.toLocal8Bit());
    if (!scanner.isEmpty()) {
        qputenv("GST_PLUGIN_SCANNER_1_0", QDir::toNativeSeparators(scanner).toLocal8Bit());
    }
#endif
}

QStringList RuntimeDependencies::missingComponents() {
    QStringList missing;
    GError* error = nullptr;
    if (!gst_init_check(nullptr, nullptr, &error)) {
        missing.push_back(error
                              ? QStringLiteral("GStreamer: %1")
                                    .arg(QString::fromUtf8(error->message))
                              : QStringLiteral("GStreamer runtime"));
        if (error) {
            g_error_free(error);
        }
        return missing;
    }

#ifdef Q_OS_WIN
    const auto bundledPlugins = QDir(QCoreApplication::applicationDirPath())
                                    .filePath(QStringLiteral("gstreamer-1.0"));
    if (QFileInfo::exists(bundledPlugins)) {
        const auto path = QDir::toNativeSeparators(bundledPlugins).toUtf8();
        gst_registry_scan_path(gst_registry_get(), path.constData());
    }
#endif

    requireFactory(missing, "GStreamer appsrc", {"appsrc"});
    requireFactory(missing, "GStreamer queue", {"queue"});
    requireFactory(missing, "GStreamer H.264 parser", {"h264parse"});
    requireFactory(missing, "GStreamer H.265 parser", {"h265parse"});
    requireFactory(missing, "D3D11 H.264 decoder", {"d3d11h264dec"});
    requireFactory(missing, "D3D11 H.265 decoder", {"d3d11h265dec"});
    requireFactory(missing, "D3D11 converter", {"d3d11convert"});
    requireFactory(missing, "GStreamer video scaler", {"videoscale"});
    requireFactory(missing, "D3D11 video sink", {"d3d11videosink"});
    requireFactory(missing, "GStreamer audio converter", {"audioconvert"});
    requireFactory(missing, "GStreamer audio resampler", {"audioresample"});
    requireFactory(missing, "GStreamer volume", {"volume"});
    requireFactory(missing, "GStreamer level", {"level"});
    requireFactory(missing, "GStreamer ALAC decoder", {"avdec_alac"});
    requireFactory(missing, "WASAPI2 audio sink", {"wasapi2sink"});

#ifdef Q_OS_WIN
    const auto fdk = QDir(QCoreApplication::applicationDirPath())
                         .filePath(QStringLiteral("libfdk-aac-2.dll"));
    if (!QFileInfo::exists(fdk)) {
        missing.push_back(QStringLiteral("libfdk-aac-2.dll (AAC-ELD game audio)"));
    }
#endif
    return missing;
}

QStringList RuntimeDependencies::featureReport() {
    return {
        QStringLiteral("TRANSPORT=AIRPLAY_ONLY"),
        QStringLiteral("BACKEND=UXPLAY_IN_PROCESS"),
        QStringLiteral("VIDEO=GSTREAMER_D3D11"),
        QStringLiteral("AUDIO=GSTREAMER_WASAPI2_FDK_AAC_ELD"),
        QStringLiteral("USB=NOT_INCLUDED"),
    };
}

} // namespace padmirror::app
