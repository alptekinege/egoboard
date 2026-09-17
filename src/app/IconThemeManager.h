#pragma once

#include <QObject>

// Installs the icon theme the whole UI draws from.
//
// Every icon in Egoboard comes from QIcon::fromTheme(), so pointing Qt at an
// icon theme restyles toolbar, menus, list entries and dialogs in one go — no
// per-widget icon bookkeeping and no bundled image assets. Discovery lives in
// IconThemeIndex; this is only the id -> Qt-globals mapping.
class IconThemeManager : public QObject {
    Q_OBJECT
public:
    // themeId: "system" (follow Plasma's active icon theme) or the id of an
    // installed theme. Qt re-resolves theme icons on the next paint, so a
    // running UI follows the change without a restart.
    static void apply(const QString &themeId);

private:
    IconThemeManager() = delete;
};
