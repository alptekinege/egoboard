#include "IconThemeIndex.h"

#include <KConfigGroup>
#include <KSharedConfig>

#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QSet>
#include <QStandardPaths>

#include <algorithm>

namespace {
constexpr QLatin1String kSystemId("system");
constexpr QLatin1String kIconDirName("icons");
constexpr QLatin1String kIndexFileName("index.theme");
constexpr QLatin1String kPlasmaDefaultId("breeze");
constexpr QLatin1String kFreedesktopFallbackId("hicolor");

// The icon theme metadata group inside a theme's index.theme.
KConfigGroup themeGroup(const QString &directory)
{
    return KSharedConfig::openConfig(QDir(directory).absoluteFilePath(kIndexFileName))
        ->group(QStringLiteral("Icon Theme"));
}

// An icon theme must declare icon directories. Cursor themes ship an
// index.theme as well, but only a cursors entry, so they are skipped.
bool isIconTheme(const QString &directory)
{
    if (!QFileInfo::exists(QDir(directory).absoluteFilePath(kIndexFileName)))
        return false;
    return !themeGroup(directory).readEntry(QStringLiteral("Directories"), QString()).isEmpty();
}

// Directory of the first installed theme with this id, empty when there is none.
QString themeDirectory(const QString &id)
{
    if (id.isEmpty() || id.contains(QLatin1Char('/')) || id.startsWith(QLatin1Char('.')))
        return QString(); // ids come from the config file and name a directory

    const QStringList roots = IconThemeIndex::iconDirectories();
    for (const QString &root : roots) {
        const QString path = QDir(root).filePath(id);
        if (isIconTheme(path))
            return path;
    }
    return QString();
}
} // namespace

QStringList IconThemeIndex::iconDirectories()
{
    QStringList directories;

    // Qt's own list first, so themes installed where the platform theme points
    // Qt keep their precedence. Resource paths (":/icons") hold no theme dirs.
    const auto append = [&directories](const QString &directory) {
        if (!directory.isEmpty() && !directory.startsWith(QLatin1Char(':'))
            && !directories.contains(directory)) {
            directories.append(directory);
        }
    };
    const QStringList qtPaths = QIcon::themeSearchPaths();
    for (const QString &path : qtPaths)
        append(path);

    // ...then the XDG icon dirs, which is where Plasma installs themes.
    const QStringList roots = QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation);
    for (const QString &root : roots)
        append(QDir(root).filePath(kIconDirName));

    return directories;
}

QVector<IconThemeIndex::Entry> IconThemeIndex::scan()
{
    QVector<Entry> entries;
    QSet<QString> seenIds;

    const QStringList directories = iconDirectories();
    for (const QString &root : directories) {
        const QDir dir(root);
        const QStringList subdirs = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString &id : subdirs) {
            if (seenIds.contains(id))
                continue; // a theme earlier in the search path wins
            const QString path = dir.absoluteFilePath(id);
            if (!isIconTheme(path))
                continue;
            seenIds.insert(id);
            entries.append({id, themeGroup(path).readEntry(QStringLiteral("Name"), id)});
        }
    }

    std::sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b) {
        const int byName = QString::compare(a.name, b.name, Qt::CaseInsensitive);
        return byName != 0 ? byName < 0 : a.id < b.id;
    });
    return entries;
}

QString IconThemeIndex::resolvedId(const QString &id)
{
    if (id != kSystemId)
        return id;
    return KSharedConfig::openConfig(QStringLiteral("kdeglobals"))
        ->group(QStringLiteral("Icons"))
        .readEntry(QStringLiteral("Theme"), QString());
}

QString IconThemeIndex::fallbackId()
{
    // Plasma's own default, then the freedesktop base theme, so icons the
    // selected theme does not ship still resolve to something.
    if (!themeDirectory(kPlasmaDefaultId).isEmpty())
        return kPlasmaDefaultId;
    if (!themeDirectory(kFreedesktopFallbackId).isEmpty())
        return kFreedesktopFallbackId;
    return QString();
}

bool IconThemeIndex::isValid(const QString &id)
{
    if (id == kSystemId)
        return true; // always selectable; it follows the desktop
    return !themeDirectory(id).isEmpty();
}
