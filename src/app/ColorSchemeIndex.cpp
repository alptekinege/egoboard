#include "ColorSchemeIndex.h"

#include <KConfigGroup>
#include <KSharedConfig>

#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <QStandardPaths>

#include <algorithm>

namespace {
constexpr QLatin1String kSystemId("system");
constexpr QLatin1String kSchemeSuffix(".colors");
constexpr QLatin1String kSchemeDirName("color-schemes");
constexpr QLatin1String kLegacyLightId("light");
constexpr QLatin1String kLegacyDarkId("dark");
constexpr QLatin1String kLegacyLightScheme("BreezeLight");
constexpr QLatin1String kLegacyDarkScheme("BreezeDark");

// Directories KDE searches for *.colors files, most specific first: the
// per-user config/data dirs, then the system dirs from $XDG_DATA_DIRS.
QStringList schemeDirectories()
{
    QStringList directories;
    const auto appendLocation = [&directories](QStandardPaths::StandardLocation location) {
        const QStringList roots = QStandardPaths::standardLocations(location);
        for (const QString &root : roots) {
            const QString directory = QDir(root).filePath(kSchemeDirName);
            if (!directories.contains(directory))
                directories.append(directory);
        }
    };
    appendLocation(QStandardPaths::GenericConfigLocation);
    appendLocation(QStandardPaths::GenericDataLocation);
    return directories;
}

// Ids are used to build file paths, so anything path-like is refused: the id
// arrives from the config file and must never leave the color-schemes dirs.
bool isPlainSchemeId(const QString &id)
{
    return !id.isEmpty() && !id.contains(QLatin1Char('/')) && !id.startsWith(QLatin1Char('.'));
}

QString displayNameOfFile(const QString &path, const QString &fallback)
{
    if (QFileInfo(path).isSymLink())
        return fallback;
    if (QFileInfo(path).size() > 256 * 1024)
        return fallback; // unbounded INI parse is a DoS vector
    QString name = KSharedConfig::openConfig(path)
                       ->group(QStringLiteral("General"))
                       .readEntry(QStringLiteral("Name"), fallback);
    name = name.trimmed().left(80);
    name.removeIf([](QChar c) {
        const ushort u = c.unicode();
        return u < 0x20 || u == 0x7F || c == QChar(0x202E) || c == QChar(0x200B);
    });
    return name.isEmpty() ? fallback : name;
}
} // namespace

QVector<ColorSchemeIndex::Entry> ColorSchemeIndex::scan()
{
    QVector<Entry> entries;
    QSet<QString> seenIds;

    const QStringList directories = schemeDirectories();
    for (const QString &directory : directories) {
        const QDir dir(directory);
        const QStringList files =
            dir.entryList({QStringLiteral("*.colors")}, QDir::Files | QDir::NoSymLinks);
        for (const QString &file : files) {
            const QString path = dir.absoluteFilePath(file);
            if (QFileInfo(path).isSymLink())
                continue; // must not escape the color-schemes dirs
            const QString id = QFileInfo(path).completeBaseName();
            if (seenIds.contains(id))
                continue; // a scheme earlier in the search path wins
            seenIds.insert(id);
            entries.append({id, displayNameOfFile(path, id), path});
        }
    }

    std::sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b) {
        const int byName = QString::compare(a.name, b.name, Qt::CaseInsensitive);
        return byName != 0 ? byName < 0 : a.id < b.id;
    });
    return entries;
}

QString ColorSchemeIndex::resolvedId(const QString &id)
{
    if (id == kLegacyLightId)
        return kLegacyLightScheme;
    if (id == kLegacyDarkId)
        return kLegacyDarkScheme;
    return id;
}

QString ColorSchemeIndex::filePath(const QString &id)
{
    const QString schemeId = id == kSystemId ? activeSystemSchemeId() : resolvedId(id);
    if (!isPlainSchemeId(schemeId))
        return QString();

    const QString fileName = schemeId + kSchemeSuffix;
    const QStringList directories = schemeDirectories();
    for (const QString &directory : directories) {
        const QString path = QDir(directory).filePath(fileName);
        const QFileInfo info(path);
        if (info.isSymLink() || !QFileInfo::exists(path))
            continue;
        // Canonical prefix check: the file must resolve inside its scheme dir.
        const QString canonDir = QFileInfo(directory).canonicalFilePath();
        const QString canonFile = info.canonicalFilePath();
        if (!canonDir.isEmpty() && !canonFile.isEmpty()
            && !canonFile.startsWith(canonDir + QLatin1Char('/')))
            continue;
        return path;
    }
    return QString();
}

bool ColorSchemeIndex::isValid(const QString &id)
{
    if (id == kSystemId || resolvedId(id) != id)
        return true; // "system" and the legacy presets always make sense
    return !filePath(id).isEmpty();
}

QString ColorSchemeIndex::activeSystemSchemeId()
{
    return KSharedConfig::openConfig(QStringLiteral("kdeglobals"))
        ->group(QStringLiteral("General"))
        .readEntry(QStringLiteral("ColorScheme"), QString());
}
