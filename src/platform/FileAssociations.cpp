// SPDX-License-Identifier: MIT
// platform — see FileAssociations.hpp.

#include "platform/FileAssociations.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QtGlobal>

namespace koalaamp::platform {
namespace {

constexpr const char* kProgId = "KoalaAmp.MediaFile";
constexpr const char* kDesktopName = "koalaamp.desktop";

QStringList extensionsFor(const QStringList& extensions) {
    QStringList cleaned;
    for (QString extension : extensions) {
        extension = extension.trimmed().toLower();
        if (extension.startsWith('.')) {
            extension.remove(0, 1);
        }
        if (!extension.isEmpty() && !cleaned.contains(extension)) {
            cleaned.push_back(extension);
        }
    }
    return cleaned;
}

QString quoted(const QString& value) {
    QString escaped = value;
    escaped.replace('\\', "\\\\");
    escaped.replace('"', "\\\"");
    escaped.replace('`', "\\`");
    escaped.replace('$', "\\$");
    return '"' + escaped + '"';
}

QString desktopExec(const QString& executable, const QString& args) {
    return quoted(executable) + args;
}

QStringList mimeTypes(const QStringList& extensions) {
    QStringList types;
    for (const QString& extension : extensions) {
        const QString mime = mimeTypeForExtension(extension);
        if (!mime.isEmpty() && !types.contains(mime)) {
            types.push_back(mime);
        }
    }
    return types;
}

FileAssociationResult fail(const QString& error) {
    FileAssociationResult result;
    result.error = error;
    return result;
}

#if defined(Q_OS_LINUX)
QString userApplicationsDir() {
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) +
           QStringLiteral("/applications");
}

QString mimeAppsPath() {
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) +
           QStringLiteral("/mimeapps.list");
}

void setMimeDefault(bool unregister) {
    QSettings settings(mimeAppsPath(), QSettings::IniFormat);
    for (const QString& mime : mimeTypes(mediaExtensions())) {
        const QString key = QStringLiteral("Default Applications/") + mime;
        if (unregister) {
            if (settings.value(key).toString() == QLatin1String(kDesktopName)) {
                settings.remove(key);
            }
        } else {
            settings.setValue(key, QLatin1String(kDesktopName));
        }
        const QString addedKey = QStringLiteral("Added Associations/") + mime;
        if (unregister) {
            if (settings.value(addedKey).toString().contains(kDesktopName)) {
                settings.remove(addedKey);
            }
        } else {
            settings.setValue(addedKey, QLatin1String(kDesktopName));
        }
    }
    settings.sync();
}

FileAssociationResult registerLinux(const QString& executable, bool unregister) {
    const QString path = userApplicationsDir() + QLatin1Char('/') + QLatin1String(kDesktopName);
    FileAssociationResult result;
    if (unregister) {
        if (QFileInfo::exists(path) && !QFile::remove(path)) {
            return fail(QStringLiteral("cannot remove %1").arg(path));
        }
        result.ok = true;
        result.details << QStringLiteral("removed ") + path;
    } else {
        if (!QDir().mkpath(userApplicationsDir())) {
            return fail(QStringLiteral("cannot create %1").arg(userApplicationsDir()));
        }
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            return fail(QStringLiteral("cannot write %1").arg(path));
        }
        file.write(linuxDesktopEntry(executable, mediaExtensions()).toUtf8());
        if (!file.commit()) {
            return fail(QStringLiteral("cannot commit %1").arg(path));
        }
        result.details << QStringLiteral("wrote ") + path;
    }
    setMimeDefault(unregister);
    result.ok = true;
    result.details << QStringLiteral("updated ") + mimeAppsPath();
    return result;
}
#endif

#if defined(Q_OS_WIN)
FileAssociationResult registerWindows(const QString& executable, bool unregister) {
    QSettings settings(QStringLiteral("HKEY_CURRENT_USER\\Software\\Classes"),
                       QSettings::NativeFormat);
    const QString prog = QLatin1String(kProgId);
    const QStringList extensions = extensionsFor(mediaExtensions());
    if (unregister) {
        for (const QString& extension : extensions) {
            const QString extKey = QStringLiteral(".") + extension;
            if (settings.value(extKey + QStringLiteral("/.")).toString() == prog) {
                settings.remove(extKey + QStringLiteral("/."));
            }
            settings.remove(extKey + QStringLiteral("/OpenWithProgids/") + prog);
        }
        settings.remove(prog);
        settings.sync();
        return {settings.status() == QSettings::NoError, QStringLiteral("registry")};
    }

    settings.setValue(prog + QStringLiteral("/."), QStringLiteral("KoalaAmp media file"));
    settings.setValue(prog + QStringLiteral("/DefaultIcon/."),
                      quoted(executable) + QStringLiteral(",0"));
    settings.setValue(prog + QStringLiteral("/shell/open/command/."),
                      windowsPlayCommand(executable));
    settings.setValue(prog + QStringLiteral("/shell/enqueue/command/."),
                      windowsEnqueueCommand(executable));
    for (const QString& extension : extensions) {
        const QString extKey = QStringLiteral(".") + extension;
        settings.setValue(extKey + QStringLiteral("/."), prog);
        settings.setValue(extKey + QStringLiteral("/OpenWithProgids/") + prog, QString());
    }
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        return fail(QStringLiteral("registry write failed"));
    }
    FileAssociationResult result;
    result.ok = true;
    result.details
        << QStringLiteral("registered KoalaAmp.MediaFile for %1 extensions").arg(extensions.size());
    return result;
}
#endif

} // namespace

QStringList mediaExtensions() {
    return {
        QStringLiteral("mp3"),  QStringLiteral("m4a"), QStringLiteral("aac"),
        QStringLiteral("flac"), QStringLiteral("ogg"), QStringLiteral("oga"),
        QStringLiteral("opus"), QStringLiteral("wav"), QStringLiteral("aiff"),
        QStringLiteral("aif"),  QStringLiteral("ape"), QStringLiteral("wv"),
        QStringLiteral("wma"),  QStringLiteral("mka"), QStringLiteral("mod"),
        QStringLiteral("xm"),   QStringLiteral("it"),  QStringLiteral("s3m"),
        QStringLiteral("umx"),  QStringLiteral("mid"), QStringLiteral("midi"),
        QStringLiteral("cue"),  QStringLiteral("m3u"), QStringLiteral("m3u8"),
        QStringLiteral("pls"),  QStringLiteral("b4s"), QStringLiteral("xspf"),
        QStringLiteral("asx"),  QStringLiteral("wax"), QStringLiteral("wvx"),
        QStringLiteral("wpl"),
    };
}

QString mimeTypeForExtension(const QString& extension) {
    const QString ext = extension.toLower();
    if (ext == QStringLiteral("mp3")) return QStringLiteral("audio/mpeg");
    if (ext == QStringLiteral("m4a")) return QStringLiteral("audio/mp4");
    if (ext == QStringLiteral("aac")) return QStringLiteral("audio/aac");
    if (ext == QStringLiteral("flac")) return QStringLiteral("audio/flac");
    if (ext == QStringLiteral("ogg") || ext == QStringLiteral("oga"))
        return QStringLiteral("audio/ogg");
    if (ext == QStringLiteral("opus")) return QStringLiteral("audio/ogg");
    if (ext == QStringLiteral("wav")) return QStringLiteral("audio/x-wav");
    if (ext == QStringLiteral("aiff") || ext == QStringLiteral("aif"))
        return QStringLiteral("audio/x-aiff");
    if (ext == QStringLiteral("wma")) return QStringLiteral("audio/x-ms-wma");
    if (ext == QStringLiteral("mka")) return QStringLiteral("audio/x-matroska");
    if (ext == QStringLiteral("m3u") || ext == QStringLiteral("m3u8"))
        return QStringLiteral("audio/x-mpegurl");
    if (ext == QStringLiteral("pls")) return QStringLiteral("audio/x-scpls");
    if (ext == QStringLiteral("xspf")) return QStringLiteral("application/xspf+xml");
    if (ext == QStringLiteral("b4s") || ext == QStringLiteral("wpl"))
        return QStringLiteral("application/xml");
    return {};
}

QString linuxDesktopEntry(const QString& executable, const QStringList& extensions) {
    QStringList types = mimeTypes(extensionsFor(extensions));
    types.sort();
    const QString mime = types.join(QLatin1Char(';')) + QLatin1Char(';');
    return QStringLiteral("[Desktop Entry]\n"
                          "Type=Application\n"
                          "Name=KoalaAmp\n"
                          "Comment=Classic-style music player\n"
                          "Exec=%1 %F\n"
                          "TryExec=%2\n"
                          "Icon=koalaamp\n"
                          "Terminal=false\n"
                          "Categories=AudioVideo;Audio;Player;\n"
                          "MimeType=%3\n"
                          "Actions=Play;Enqueue;\n"
                          "\n"
                          "[Desktop Action Play]\n"
                          "Name=Play in KoalaAmp\n"
                          "Exec=%1 --play %F\n"
                          "\n"
                          "[Desktop Action Enqueue]\n"
                          "Name=Enqueue in KoalaAmp\n"
                          "Exec=%1 --enqueue %F\n")
        .arg(quoted(executable), executable, mime);
}

QString windowsPlayCommand(const QString& executable) {
    return quoted(executable) + QStringLiteral(" --play \"%1\"");
}

QString windowsEnqueueCommand(const QString& executable) {
    return quoted(executable) + QStringLiteral(" --enqueue \"%1\"");
}

FileAssociationResult registerFileAssociations(const QString& executable, bool unregister) {
#if defined(Q_OS_LINUX)
    return registerLinux(executable, unregister);
#elif defined(Q_OS_WIN)
    return registerWindows(executable, unregister);
#elif defined(Q_OS_MACOS)
    Q_UNUSED(executable);
    Q_UNUSED(unregister);
    return fail(QStringLiteral(
        "macOS document types live in the app bundle Info.plist; install the packaged bundle"));
#else
    Q_UNUSED(executable);
    Q_UNUSED(unregister);
    return fail(QStringLiteral("file associations are not supported on this platform"));
#endif
}

} // namespace koalaamp::platform
