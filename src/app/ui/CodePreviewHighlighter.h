#pragma once
#include <QColor>
#include <QPalette>
#include <QSyntaxHighlighter>
#include <QTextDocument>

// Lightweight, offline highlighter for code-like clipboard entries.
// Detects JSON, XML, and generic code heuristics; highlights without network.
class CodePreviewHighlighter : public QSyntaxHighlighter {
    Q_OBJECT
public:
    enum class Mode { Plain, Json, Xml, Code };

    // The five code roles. Derived from the active palette instead of a fixed
    // VS-Code preset pair, so high-contrast and mid-lightness schemes get a set
    // that actually reads on their own surface.
    struct Theme {
        QColor string;
        QColor key;
        QColor number;
        QColor keyword;
        QColor comment;

        bool operator==(const Theme &other) const = default;
    };
    static Theme themeFor(const QPalette &palette);

    explicit CodePreviewHighlighter(QTextDocument *doc);
    void setMode(Mode mode);
    static Mode detect(const QString &text);
protected:
    void highlightBlock(const QString &text) override;
private:
    Mode m_mode = Mode::Plain;
};
