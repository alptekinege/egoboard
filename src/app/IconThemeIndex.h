#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

// Which icon themes the app can wear.
//
// Icons are never bundled or hardcoded: the whole UI resolves them through
// QIcon::fromTheme(), so it inherits whatever icon theme the user runs. This
// unit lists the themes installed on the machine (the same XDG icon dirs Qt
// searches) so the settings dialog can offer them, and hands out the Plasma
// default used when an icon is missing. Installing a selection into Qt is
// IconThemeManager's job.
namespace IconThemeIndex {

// Theme ids understood by the app:
//   "system"       -> follow the icon theme Plasma has active right now
//   anything else  -> id of an installed theme: its directory name under one of
//                     the icon search paths, which is what QIcon::setThemeName()
//                     expects

struct Entry {
    QString id;
    QString name; // [Icon Theme] Name= of index.theme, falling back to the id
};

// Installed icon themes, sorted by display name. Directories without an
// index.theme are skipped, and so are cursor-only themes: they carry an
// index.theme but no icon directories, so they cannot draw a single toolbar
// icon.
QVector<Entry> scan();

// Directories that hold icon themes, most specific first: what Qt was told to
// search, plus the XDG icon dirs (Qt only knows those once a platform theme
// passes them on, which is not guaranteed outside a Plasma session).
QStringList iconDirectories();

// Maps "system" to the icon theme Plasma has active; other ids pass through.
// Empty when the desktop has no icon theme configured.
QString resolvedId(const QString &id);

// Theme used for icons the selected theme does not ship: Plasma's default
// ("breeze"), else the freedesktop fallback. Empty when neither is installed.
QString fallbackId();

// True when the id can be applied; "system" is always valid.
bool isValid(const QString &id);

} // namespace IconThemeIndex
