#include "AutoPaster.h"

#include "ClipboardWatcher.h"

#include <KNotification>

#include <QClipboard>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMimeData>
#include <QProcess>
#include <QStandardPaths>
#include <QTextDocument>
#include <QTimer>

#ifdef EGOBOARD_HAVE_XTEST
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/extensions/XTest.h>
#endif

namespace {

// Let the window manager hand keyboard focus back to the previous window
// before we inject Ctrl+V.
constexpr int kFocusReturnDelayMs = 180;

std::unique_ptr<QMimeData> buildMimeData(const ClipboardRecord &record)
{
    auto mime = std::make_unique<QMimeData>();
    switch (record.type) {
    case ContentType::Text:
        mime->setText(record.textData);
        break;
    case ContentType::RichText: {
        mime->setHtml(record.textData);
        QTextDocument document;
        document.setHtml(record.textData);
        mime->setText(document.toPlainText());
        break;
    }
    case ContentType::Image: {
        if (!record.hasBlob)
            return nullptr;
        QImage image;
        image.loadFromData(record.blobData, "PNG");
        if (image.isNull())
            return nullptr;
        mime->setImageData(image);
        break;
    }
    case ContentType::Files: {
        const QJsonArray array = QJsonDocument::fromJson(record.textData.toUtf8()).array();
        QList<QUrl> urls;
        QStringList paths;
        for (const auto &value : array) {
            const QString path = value.toString();
            paths.append(path);
            urls.append(QUrl::fromLocalFile(path));
        }
        mime->setUrls(urls);
        mime->setText(paths.join(QLatin1Char('\n')));
        break;
    }
    }
    return mime;
}

} // namespace

AutoPaster::AutoPaster(ClipboardWatcher *watcher, QObject *parent)
    : QObject(parent)
    , m_watcher(watcher)
{
}

bool AutoPaster::canSimulateKeys()
{
#ifdef EGOBOARD_HAVE_XTEST
    if (QGuiApplication::platformName() == QLatin1String("xcb"))
        return true;
#endif
    return QStandardPaths::findExecutable(QStringLiteral("xdotool")) != QString();
}

void AutoPaster::paste(const ClipboardRecord &record, QWidget *windowToHide)
{
    auto mime = buildMimeData(record);
    if (!mime) {
        emit failed(QObject::tr("This entry has no stored payload (it exceeded the size limit)."));
        return;
    }

    if (m_watcher)
        m_watcher->suppressOwnSets();
    QGuiApplication::clipboard()->setMimeData(mime.release(), QClipboard::Clipboard);

    // Give the focus back to the user's app before injecting keys.
    if (windowToHide)
        windowToHide->hide();

    QTimer::singleShot(kFocusReturnDelayMs, this, [this, id = record.id] {
        simulateCtrlV();
        emit pasted(id);
    });
}

void AutoPaster::simulateCtrlV()
{
    const bool isX11 = QGuiApplication::platformName() == QLatin1String("xcb");
    if (!isX11) {
        KNotification::event(
            QStringLiteral("pasteReady"),
            QObject::tr("Ready to paste"),
            QObject::tr("Copied to clipboard — press Ctrl+V to paste it."),
            QStringLiteral("edit-paste"),
            KNotification::CloseOnTimeout);
        return;
    }

#ifdef EGOBOARD_HAVE_XTEST
    if (xtestPaste())
        return;
#endif
    if (xdotoolPaste())
        return;

    KNotification::event(
        QStringLiteral("pasteReady"),
        QObject::tr("Ready to paste"),
        QObject::tr("Copied to clipboard — press Ctrl+V to paste it."),
        QStringLiteral("edit-paste"),
        KNotification::CloseOnTimeout);
}

bool AutoPaster::xtestPaste()
{
#ifdef EGOBOARD_HAVE_XTEST
    Display *display = XOpenDisplay(nullptr);
    if (!display)
        return false;
    const KeyCode control = XKeysymToKeycode(display, XK_Control_L);
    const KeyCode v = XKeysymToKeycode(display, XK_V);
    if (control != 0 && v != 0) {
        XTestFakeKeyEvent(display, control, True, CurrentTime);
        XTestFakeKeyEvent(display, v, True, CurrentTime);
        XTestFakeKeyEvent(display, v, False, CurrentTime);
        XTestFakeKeyEvent(display, control, False, CurrentTime);
        XFlush(display);
    }
    XCloseDisplay(display);
    return control != 0 && v != 0;
#else
    return false;
#endif
}

bool AutoPaster::xdotoolPaste()
{
    if (!m_xdotoolChecked) {
        m_xdotoolAvailable =
            QStandardPaths::findExecutable(QStringLiteral("xdotool")) != QString();
        m_xdotoolChecked = true;
    }
    if (!m_xdotoolAvailable)
        return false;
    QProcess::startDetached(QStringLiteral("xdotool"),
                            {QStringLiteral("key"), QStringLiteral("--clearmodifiers"),
                             QStringLiteral("ctrl+v")});
    return true;
}
