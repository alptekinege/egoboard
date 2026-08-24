#pragma once

#include <QObject>
#include <QPalette>

class SettingsManager;

// Applies the configured theme ("system" | "light" | "dark") to the
// application palette. Palette-based so it composes cleanly with the Breeze
// style instead of overriding it with a full QSS skin.
class ThemeManager : public QObject {
    Q_OBJECT
public:
    static void apply(const QString &theme, SettingsManager *settings);

private:
    ThemeManager() = delete;
};
