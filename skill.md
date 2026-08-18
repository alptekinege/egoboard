---
name: egoboard-style-guide
description: Comprehensive development skill and workflow guide for writing idiomatic C++20, Qt 6, and KDE Frameworks 6 code in the Egoboard repository. Use this skill when developing new features, creating core modules, implementing UI components, interacting with SQLite, or writing unit tests for Egoboard.
---

# Egoboard Development & Writing Style Skill

This skill provides step-by-step instructions, code recipes, architectural patterns, and quality checklists for developing, modifying, and maintaining code within the **Egoboard** codebase.

---

## 1. Architectural Principles & Component Boundaries

Egoboard is structured with strict layering to maximize testability, platform independence, and separation of concerns:

```
src/
├── core/       # egoboard_core: Qt Core + Sql only (GUI-less, strictly unit-testable)
├── app/        # Desktop integration: Watcher, trackers, hotkeys, tray, settings, AutoPaster
│   └── ui/     # Qt Widgets presentation layer (MainWindow, PreviewPane, Delegates, Dialogs)
└── main.cpp    # Application bootstrap, CLI argument parser (--smoke), SingleInstanceGuard
```

### Key Architectural Rules:
1. **`egoboard_core` Purity**:
   - `src/core/` MUST depend only on `Qt6::Core` and `Qt6::Sql`.
   - Never include GUI headers (`QWidget`, `QPainter`, `QIcon`, `QApplication`, `KF6*`) in core classes, with the sole exception of `GroupTreeModel` which requires `QIcon` for `Qt::DecorationRole`.
   - Core classes must be runnable in headless unit tests (`QTEST_GUILESS_MAIN`).
2. **Interface Seams & Inversion of Control**:
   - Abstract cross-cutting dependencies behind interfaces inheriting from `QObject` or having virtual destructors (e.g., `IClipboardStorage`, `IActiveWindowTracker`).
   - Allow mock/in-memory implementations for deterministic testing.
3. **Composition Root**:
   - `ApplicationContext` (`src/app/ApplicationContext.h`) is the central composition root that instantiates subsystems, wires signals/slots, and coordinates the lifecycle.
4. **Thread Safety & Multi-Threading**:
   - SQLite access across threads MUST use separate connections (e.g., `StorageManager` on the main GUI thread, `VacuumWorker` on a dedicated `QThread`).
   - Move worker objects to threads (`worker->moveToThread(thread)`) and connect `thread->finished` to `worker->deleteLater`.

---

## 2. C++20 & Qt 6 Code Style Recipes

### A. Header File Template (`.h`)
Always follow this exact structure for header files:

```cpp
#pragma once

#include "ContentType.h"
#include "IClipboardStorage.h"

#include <QObject>
#include <QString>
#include <QVector>

#include <memory>
#include <optional>

class StorageManager;

/**
 * @brief Brief 1-2 sentence description explaining the class purpose and threading model.
 */
class FeatureManager : public QObject {
    Q_OBJECT
public:
    enum class State : qint8 {
        Idle = 0,
        Active = 1,
    };
    Q_ENUM(State)

    explicit FeatureManager(StorageManager *storage, QObject *parent = nullptr);
    ~FeatureManager() override;

    // Getters follow Qt style: no 'get' prefix
    State state() const { return m_state; }
    bool isEnabled() const { return m_enabled; }

    // Setters
    void setEnabled(bool enabled);

    // Business operations
    bool processItem(qint64 id, QString *error = nullptr);

signals:
    void stateChanged(State newState);
    void itemProcessed(qint64 id);

private:
    StorageManager *m_storage = nullptr;
    State m_state = State::Idle;
    bool m_enabled = true;
};
```

### B. Source File Template (`.cpp`)
Always follow this structure for implementation files:

```cpp
#include "FeatureManager.h"

#include "StorageManager.h"

#include <QDateTime>
#include <QLoggingCategory>

namespace {

// Anonymous namespace for file-local constants and pure helper functions
constexpr int kMaxRetryCount = 3;
constexpr qint64 kTimeoutMs = 5000;

QString formatErrorMessage(int code, const QString &detail)
{
    return QStringLiteral("Error %1: %2").arg(code).arg(detail);
}

} // namespace

FeatureManager::FeatureManager(StorageManager *storage, QObject *parent)
    : QObject(parent)
    , m_storage(storage)
{
}

FeatureManager::~FeatureManager() = default;

void FeatureManager::setEnabled(bool enabled)
{
    if (m_enabled == enabled)
        return;

    m_enabled = enabled;
    emit stateChanged(m_enabled ? State::Active : State::Idle);
}

bool FeatureManager::processItem(qint64 id, QString *error)
{
    if (!m_enabled) {
        if (error)
            *error = QStringLiteral("Feature is currently disabled");
        return false;
    }

    // Logic here...
    emit itemProcessed(id);
    return true;
}
```

---

## 3. Specific Subsystem Implementation Patterns

### Pattern 1: SQLite Operations & Queries
- **Transactions**: Wrap multi-statement updates in `m_db.transaction()` and `m_db.commit()`.
- **Prepared Statements**: ALWAYS use `query.prepare(...)` and `query.bindValue(...)` with named placeholders (`:param`). Never concatenate raw SQL strings.
- **Escape LIKE Queries**: When filtering user input, use the project `likeEscape` helper:
  ```cpp
  QString likeEscape(const QString &text) {
      QString out;
      out.reserve(text.size() * 2);
      for (const QChar c : text) {
          if (c == QLatin1Char('%') || c == QLatin1Char('_') || c == QLatin1Char('\\'))
              out += QLatin1Char('\\');
          out += c;
      }
      return out;
  }
  ```
- **Keyset Pagination**: For infinite scrolling lists, use keyset cursors `(timestamp_ms < :cursor_ts OR (timestamp_ms = :cursor_ts AND id < :cursor_id)) ORDER BY timestamp_ms DESC, id DESC LIMIT :limit` instead of `OFFSET`.

### Pattern 2: Platform Abstraction (Wayland vs. X11)
- **Active Window Detection**:
  - X11: Use `KWindowSystem` and `_NET_WM_PID` -> `/proc/<pid>/comm` or `KWindowInfo::name()`.
  - Wayland: Use `zwlr_foreign_toplevel_manager_v1` via QtWayland client extension (`WaylandActiveWindowTracker`). Note that Wayland yields `app_id` (e.g. `org.kde.kate`), not PID.
- **Auto-Paste Simulation**:
  - X11: Simulate `Ctrl+V` key events via XTest (when compiled with `EGOBOARD_HAVE_XTEST`) or spawn `xdotool key --clearmodifiers ctrl+v`.
  - Wayland: Wayland security model disallows synthetic key injection for unprivileged clients. Re-set the clipboard and display a `KNotification` informing the user to press `Ctrl+V`.

### Pattern 3: Settings Management with KConfig
- Use `KConfig` in `NoGlobals` mode with dedicated groups (e.g., `"General"`, `"History"`).
- Provide strongly-typed accessor methods and emit `changed()` signals when configurations mutate:
  ```cpp
  bool SettingsManager::monitorPrimarySelection() const {
      return m_config->group(QStringLiteral("General")).readEntry("MonitorPrimarySelection", false);
  }
  void SettingsManager::setMonitorPrimarySelection(bool monitor) {
      m_config->group(QStringLiteral("General")).writeEntry("MonitorPrimarySelection", monitor);
      save();
  }
  ```

### Pattern 4: Custom UI Delegates (`QStyledItemDelegate`)
- In `paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index)`:
  1. Call `initStyleOption(&opt, index)`.
  2. Clear `opt.text` and `opt.icon` to let the style engine render selection, focus, and hover backgrounds only.
  3. Draw custom elements (icons, badges, relative timestamps, preview text) manually using `painter->drawText`, `painter->drawPixmap`, etc.
  4. Respect system palette colors: `option.palette.color(QPalette::Text)`, `option.palette.color(QPalette::HighlightedText)`.

---

## 4. Testing Workflow & Verification Recipes

### Writing a Core Unit Test (`tests/tst_*.cpp`)
Every test suite must be completely self-contained and run on a temporary SQLite database:

```cpp
#include <QtTest>
#include "StorageManager.h"
#include <QTemporaryDir>

class TestMyFeature : public QObject {
    Q_OBJECT
private slots:
    void init();
    void testBasicOperation();

private:
    QTemporaryDir m_dir;
    StorageManager *m_storage = nullptr;
};

void TestMyFeature::init()
{
    delete m_storage;
    const QString path = m_dir.filePath(QStringLiteral("test-%1.db").arg(QRandomGenerator::global()->generate64()));
    m_storage = new StorageManager(path);
}

void TestMyFeature::testBasicOperation()
{
    QVERIFY(m_storage != nullptr);
    // Assert conditions using QVERIFY, QCOMPARE
    QCOMPARE(m_storage->stats().entryCount, qint64(0));
}

QTEST_GUILESS_MAIN(TestMyFeature)
#include "tst_myfeature.moc"
```

### Verification Commands:
Run verification in the build directory:
```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
./build/src/egoboard --smoke
```

---

## 5. Development Checklist

Before finalizing any changes or adding new files:
- [ ] Header has `#pragma once` and includes are ordered logically.
- [ ] No UI or KF6 dependencies introduced into `src/core/`.
- [ ] String literals use `QStringLiteral`, `QByteArrayLiteral`, or `QLatin1Char`.
- [ ] User-facing strings wrapped in `tr(...)` or `QObject::tr(...)`.
- [ ] Class member variables follow `m_variableName` format; constants follow `kConstantName`.
- [ ] Getters have no `get` prefix; setters use `set`.
- [ ] SQLite queries are prepared and sanitized; WAL mode and transactions used where appropriate.
- [ ] Destructors properly release QSqlDatabase handles before removing connections.
- [ ] Unit test added under `tests/` and registered in `tests/CMakeLists.txt`.
- [ ] `--smoke` self-check passes cleanly.
