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
    return KSharedConfig::openConfig(path)
        ->group(QStringLiteral("General"))
        .readEntry(QStringLiteral("Name"), fallback);
}
} // namespace

QVector<ColorSchemeIndex::Entry> ColorSchemeIndex::scan()
{
    QVector<Entry> entries;
    QSet<QString> seenIds;

    const QStringList directories = schemeDirectories();
    for (const QString &directory : directories) {
        const QDir dir(directory);
        const QStringList files = dir.entryList({QStringLiteral("*.colors")}, QDir::Files);
        for (const QString &file : files) {
            const QString path = dir.absoluteFilePath(file);
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
        if (QFileInfo::exists(path))
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
