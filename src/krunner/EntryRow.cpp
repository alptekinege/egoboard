#include "EntryRow.h"

#include <QStringList>

QString EntryRow::contentTypeId(ContentType type)
{
    switch (type) {
    case ContentType::Text:
        return QStringLiteral("text");
    case ContentType::RichText:
        return QStringLiteral("html");
    case ContentType::Image:
        return QStringLiteral("image");
    case ContentType::Files:
        return QStringLiteral("files");
    }
    return QStringLiteral("text");
}

QString EntryRow::encode() const
{
    QString safePreview = preview.left(kMaxPreviewChars);
    safePreview.replace(QLatin1Char('\t'), QLatin1Char(' '));
    return QStringList{ QString::number(id), type, sourceApp, sourceWindow,
                        pinned ? QStringLiteral("1") : QStringLiteral("0"),
                        QString::number(timestamp), safePreview }
        .join(QLatin1Char('\t'));
}

EntryRow EntryRow::decode(const QString &line)
{
    const QStringList fields = line.split(QLatin1Char('\t'));
    if (fields.size() < 7)
        return {}; // not a detailed row (the plain Search format has two fields)

    EntryRow row;
    bool ok = false;
    row.id = fields.at(0).toLongLong(&ok);
    if (!ok)
        return {};
    row.type = fields.at(1);
    row.sourceApp = fields.at(2);
    row.sourceWindow = fields.at(3);
    row.pinned = fields.at(4) == QLatin1String("1");
    row.timestamp = fields.at(5).toLongLong();
    // Extra fields a newer adaptor may have appended stay attached to the
    // preview field, which is what the runner displays anyway.
    row.preview = fields.mid(6).join(QLatin1Char('\t'));
    return row;
}

QString EntryRow::summary() const
{
    QStringList parts;
    if (!sourceApp.isEmpty())
        parts << sourceApp;
    if (!type.isEmpty())
        parts << type;
    return parts.join(QStringLiteral(" · "));
}

QString EntryRow::matchCategory() const
{
    return categoryForType(type);
}

QString EntryRow::categoryForType(const QString &typeId)
{
    if (typeId == QLatin1String("image"))
        return QStringLiteral("images");
    if (typeId == QLatin1String("files"))
        return QStringLiteral("files");
    return QStringLiteral("text"); // plain + rich text, and anything unknown
}
