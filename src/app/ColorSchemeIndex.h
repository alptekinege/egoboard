#pragma once

#include <QString>
#include <QVector>

// Where Egoboard gets its themes from.
//
// The app defines no colors of its own: a theme is a KDE *.colors file that is
// already installed on the system. This unit scans the same XDG directories
// Plasma reads, so the palette follows the desktop and any scheme the user
// installs later becomes usable without a code change.
//
// QtCore + KConfig only (no Widgets), so the settings layer can validate a
// stored theme id headlessly; installing a scheme into the application palette
// is ThemeManager's job.
namespace ColorSchemeIndex {

// Theme ids understood by the app:
//   "system"       -> follow the color scheme Plasma has active right now
//   "light"/"dark" -> legacy presets, resolved to BreezeLight/BreezeDark
//   anything else  -> id of an installed scheme: its *.colors file name without
//                     the suffix, which is what kdeglobals' ColorScheme= holds

struct Entry {
    QString id;
    QString name; // [General] Name= of the file, falling back to the id
    QString path; // absolute path of the *.colors file
};

// Installed schemes, sorted by display name. Directories are walked in KDE's
// precedence order and the first hit per id wins, so per-user schemes shadow
// system-wide ones.
QVector<Entry> scan();

// Maps a legacy preset id to the scheme it resolves to; other ids pass through.
QString resolvedId(const QString &id);

// Path of the *.colors file behind a theme id, empty when nothing matches.
QString filePath(const QString &id);

// True when the id can be applied. "system" and the legacy presets stay valid
// even when no matching file exists — apply() then falls back to the style.
bool isValid(const QString &id);

// [General] ColorScheme= from the user's kdeglobals, empty when unset.
QString activeSystemSchemeId();

} // namespace ColorSchemeIndex
