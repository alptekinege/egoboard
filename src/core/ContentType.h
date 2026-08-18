#pragma once

#include <QtGlobal>

enum class ContentType : qint8 {
    Text = 0,
    RichText = 1,
    Image = 2,
    Files = 3,
};

constexpr inline const char *contentTypeTag(ContentType type) noexcept
{
    switch (type) {
    case ContentType::Text:
        return "text";
    case ContentType::RichText:
        return "richtext";
    case ContentType::Image:
        return "image";
    case ContentType::Files:
        return "files";
    }
    return "text";
}
