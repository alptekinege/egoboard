#include <QtTest>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>

// P1 packaging verification: the shipped desktop entry is a valid,
// installable file and the packaging helper scripts exist and run.
class TestPackaging : public QObject
{
    Q_OBJECT

private slots:
    void desktopEntryIsValid();
    void helperScriptsExistAndRun();
};

namespace {
// Repository root, whatever the build directory is: prefer the QTest source
// dir, then walk up from the test binary.
QString sourceRoot()
{
    const QString fromEnv = QString::fromLocal8Bit(qgetenv("QT_TESTCASE_SOURCEDIR"));
    const QString appDir = QCoreApplication::applicationDirPath();
    for (const QString &candidate : {fromEnv + QStringLiteral("/.."),
                                     appDir + QStringLiteral("/../.."),
                                     appDir + QStringLiteral("/..")}) {
        const QDir dir(candidate);
        if (QFile::exists(dir.filePath(QStringLiteral("data/org.egoboard.Egoboard.desktop"))))
            return dir.canonicalPath();
    }
    return {};
}

QMap<QString, QString> readDesktopEntry(const QString &path)
{
    QMap<QString, QString> entries;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return entries;
    bool inHeader = false;
    while (!file.atEnd()) {
        const QString line = QString::fromUtf8(file.readLine()).trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;
        if (line.startsWith(QLatin1Char('['))) {
            inHeader = line == QLatin1String("[Desktop Entry]");
            continue;
        }
        if (!inHeader)
            continue;
        const int equals = line.indexOf(QLatin1Char('='));
        if (equals > 0)
            entries.insert(line.left(equals), line.mid(equals + 1));
    }
    return entries;
}
} // namespace

void TestPackaging::desktopEntryIsValid()
{
    const QString root = sourceRoot();
    QVERIFY2(!root.isEmpty(), "cannot locate the repository root from the test binary");
    const QMap<QString, QString> entry =
        readDesktopEntry(root + QStringLiteral("/data/org.egoboard.Egoboard.desktop"));
    QVERIFY(!entry.isEmpty());
    // Required keys for an installable application entry.
    QCOMPARE(entry.value(QStringLiteral("Type")), QStringLiteral("Application"));
    QVERIFY(!entry.value(QStringLiteral("Name")).isEmpty());
    QVERIFY(!entry.value(QStringLiteral("Exec")).isEmpty());
    QVERIFY(!entry.value(QStringLiteral("Icon")).isEmpty());
    // Booleans parse, and lists follow the trailing-semicolon convention.
    QVERIFY(entry.value(QStringLiteral("Terminal")) == QLatin1String("true")
            || entry.value(QStringLiteral("Terminal")) == QLatin1String("false"));
    QVERIFY(entry.value(QStringLiteral("Categories")).endsWith(QLatin1Char(';')));
    QVERIFY(entry.value(QStringLiteral("Categories")).contains(QStringLiteral("Utility")));
    // No deprecated keys that installers warn about.
    QVERIFY(!entry.contains(QStringLiteral("Encoding")));
}

void TestPackaging::helperScriptsExistAndRun()
{
    const QString root = sourceRoot();
    QVERIFY(!root.isEmpty());
    // Build, test and packaging entry points: present and executable.
    for (const QString &script :
         {QStringLiteral("scripts/build.sh"), QStringLiteral("scripts/test.sh"),
          QStringLiteral("scripts/build-appimage.sh"),
          QStringLiteral("scripts/validate-appimage.sh")}) {
        const QFileInfo info(root + QLatin1Char('/') + script);
        QVERIFY2(info.isFile(), qPrintable(script));
        QVERIFY2(info.isExecutable(), qPrintable(script));
    }
}

QTEST_GUILESS_MAIN(TestPackaging)
#include "tst_packaging.moc"
