#pragma once
#include <QSyntaxHighlighter>
#include <QTextDocument>

// Lightweight, offline highlighter for code-like clipboard entries.
// Detects JSON, XML, and generic code heuristics; highlights without network.
class CodePreviewHighlighter : public QSyntaxHighlighter {
    Q_OBJECT
public:
    enum class Mode { Plain, Json, Xml, Code };
    explicit CodePreviewHighlighter(QTextDocument *doc);
    void setMode(Mode mode);
    static Mode detect(const QString &text);
protected:
    void highlightBlock(const QString &text) override;
private:
    Mode m_mode = Mode::Plain;
};
