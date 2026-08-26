# Egoboard Agent Instructions & Style Guide

This document defines the strict writing style, coding standards, architectural boundaries, and engineering practices required when developing, refactoring, or maintaining the **Egoboard** repository.

---

## 1. Project Overview & Technology Stack

**Egoboard** is a native KDE Plasma clipboard history manager engineered with modern C++ and Qt/KF6. It provides unlimited SQLite-backed history, source application tracking, fast two-pane browsing, quick-paste popup navigation, and rich categorization.

- **Language Standard**: C++20 (`-std=c++20`, `CMAKE_CXX_STANDARD 20`)
- **Build System**: CMake ≥ 3.24 + Ninja
- **Core Frameworks**:
  - **Qt 6**: `Core`, `Gui`, `Widgets`, `Sql`, `DBus`, `Concurrent`, `Network`, `WaylandClient`
  - **KDE Frameworks 6 (KF6)**: `ConfigCore`, `GlobalAccel`, `Notifications`, `StatusNotifierItem`, `WindowSystem`, `XmlGui`
  - **Wayland**: `wayland-client` + `wayland-scanner` (vendored `wlr-foreign-toplevel-management-unstable-v1.xml`)
  - **X11 / XTest**: `libXtst` (optional build-time auto-paste simulation) + `xdotool` (runtime fallback)
  - **Database**: SQLite 3 (WAL mode, foreign keys enabled); optional SQLCipher (`-DEGOBOARD_USE_SQLCIPHER=ON`, KWallet key)

---

## 2. Architectural Structure & Module Boundaries

The codebase is strictly layered into distinct modules:

```
src/
├── core/                  # egoboard_core (STATIC library)
│   ├── IClipboardStorage.h       # Interface for history storage operations
│   ├── StorageManager.{h,cpp}    # SQLite implementation with WAL mode & dedup
│   ├── DatabaseSchema.{h,cpp}    # DDL & index creation
│   ├── VacuumWorker.{h,cpp}      # Dedicated-thread DB compaction
│   ├── BookmarkManager.{h,cpp}   # Pinned status & hierarchical group tree
│   ├── GroupTreeModel.{h,cpp}    # QAbstractItemModel for bookmark groups
│   ├── ExportImportManager.{h,cpp} # JSON export/import & merge engine
│   ├── SensitiveDataDetector.{h,cpp} # Luhn card & credential regex scanner
│   ├── ClipboardListModel.{h,cpp}# Virtualized paged model for QListView
│   ├── ClipboardRecord.h         # Value type for clipboard records
│   ├── ContentType.h             # Enum: Text, RichText, Image, Files
│   └── FilterSpec.h              # Search query & filter specifications
│
├── app/                   # Desktop runtime & platform integration
│   ├── ApplicationContext.{h,cpp} # Composition root & dependency wiring
│   ├── ClipboardWatcher.{h,cpp}  # QClipboard listener & debounce manager
│   ├── AutoPaster.{h,cpp}        # Clipboard restoration & key injection
│   ├── HotkeyManager.{h,cpp}     # KGlobalAccel global shortcut manager
│   ├── TrayController.{h,cpp}    # KStatusNotifierItem system tray integration
│   ├── SettingsManager.{h,cpp}   # KConfig abstraction for ~/.config/egoboardrc
│   ├── SingleInstanceGuard.{h,cpp} # Lock file & local socket communication
│   ├── ActiveWindowTracker.h     # Interface for window metadata tracking
│   ├── X11ActiveWindowTracker.{h,cpp}     # X11 / EWMH active window tracker
│   ├── WaylandActiveWindowTracker.{h,cpp} # Wayland wlr-foreign-toplevel tracker
│   └── ui/                       # Qt Widgets presentation layer
│       ├── MainWindow.{h,cpp}          # Main two-pane browser window
│       ├── EntryDelegate.{h,cpp}       # High-performance custom item delegate
│       ├── PreviewPane.{h,cpp}         # Multi-format preview stack
│       ├── QuickPasteMenu.{h,cpp}      # Frameless overlay with numeric shortcuts
│       ├── GroupsDock.{h,cpp}          # Collapsible tree dock with drag & drop
│       ├── SettingsDialog.{h,cpp}      # Multi-tab preferences dialog
│       └── ExportImportDialogs.{h,cpp} # Export/import modal dialogs
│
└── main.cpp               # Application entrypoint & --smoke self-test
```

### Strict Boundary Rules:
1. **Core Library Isolation**:
   - `src/core/` builds as the standalone library `egoboard_core` and links **only** against `Qt6::Core` and `Qt6::Sql`.
   - **Never** add UI (`QtWidgets`, `QPainter`, `QWidget`, `QApplication`) or `KF6*` headers to `src/core/` files.
   - Exception: `GroupTreeModel` uses `QIcon` for `Qt::DecorationRole` and is compiled into the app target.
   - Encryption: `DatabaseSchema` (`PRAGMA key/rekey/cipher_version`) and `StorageManager` (`setEncryptionKey`) stay headless; KWallet access lives in `src/app/EncryptionManager` only (guarded by `EGOBOARD_HAVE_SQLCIPHER`).
2. **Interface Seams**:
   - Use abstract interfaces (`IClipboardStorage`, `IActiveWindowTracker`) to allow unit testing with fakes and isolate platform-specific logic.
3. **Composition Root**:
   - Subsystem creation and cross-module signal/slot wiring belong strictly in `ApplicationContext`. Do not create cyclic dependencies across controllers.

---

## 3. C++20 & Qt 6 Coding Standards

### A. Naming Conventions
- **Types / Classes / Structs / Enums**: `PascalCase` (e.g., `ClipboardRecord`, `StorageManager`, `SensitiveDataDetector`).
- **Scoped Enums**: `enum class EnumName : qint8 { ValueOne, ValueTwo };` (always strongly typed and `PascalCase`).
- **Methods / Functions**: `camelCase` (e.g., `insertOrUpdate`, `enforceDiskCap`).
- **Getters**: Qt-style without `get` prefix (e.g., `databasePath()`, `startVisible()`, `activeWindow()`).
- **Setters**: Prefix with `set` (e.g., `setStartVisible(bool visible)`, `setDebounceInterval(int ms)`).
- **Member Variables**: Prefix with `m_` (e.g., `m_path`, `m_db`, `m_settings`, `m_watcher`).
- **File-Local Constants**: Prefix with `k` in `PascalCase` or `kCamelCase` (e.g., `kVacuumSizeThresholdBytes`, `kDiskCapCheckInterval`, `kMaxPathCount`).
- **Namespaces**: `PascalCase` for named namespaces (e.g., `namespace DatabaseSchema`, `namespace ExportImportDialogs`).

### B. Include Structure & Header Guards
- Every header must start with `#pragma once`.
- Structure includes in distinct blocks separated by single blank lines:
  1. Matching class header (in `.cpp` files)
  2. Internal project headers
  3. KDE Framework headers (`<KConfig>`, `<KNotification>`, `<KGlobalAccel>`)
  4. Qt headers (`<QString>`, `<QVector>`, `<QSqlDatabase>`)
  5. Standard library headers (`<memory>`, `<optional>`, `<atomic>`)
  6. Platform-specific / conditional headers (`#ifdef EGOBOARD_HAVE_XTEST ... #endif`)

### C. String Literals & Performance
- **Compile-time UTF-16 strings**: ALWAYS use `QStringLiteral("...")` for string constants passed to Qt methods.
- **Byte arrays / MIME types / Hashes**: ALWAYS use `QByteArrayLiteral("...")`.
- **Character literals**: ALWAYS use `QLatin1Char('x')`.
- **Latin1 string comparisons**: Use `QLatin1String("...")`.
- **Translatable strings**: Wrap all user-visible text in `tr("...")` or `QObject::tr("...")`.
- **Pre-allocations**: Use `.reserve(...)` on `QString`, `QVector`, or `QList` when appending in loops.

### D. Memory Management & Object Ownership
- **Qt Parent Hierarchy**: Pass `QObject *parent = nullptr` in constructors to leverage Qt's automatic object tree deletion.
- **Smart Pointers**: Use `std::unique_ptr` for exclusive ownership where Qt parentage is not applicable, and `std::shared_ptr` only when sharing interfaces across classes (e.g., `std::shared_ptr<IActiveWindowTracker>`).
- **Database Handles**: Explicitly reset `QSqlDatabase` local variables before calling `QSqlDatabase::removeDatabase(connectionName)` to prevent connection leak warnings.

### E. Signals and Modern Connections
- Use the modern Qt 5/6 function-pointer syntax for signal/slot connections:
  ```cpp
  connect(m_watcher, &ClipboardWatcher::captured, this, &ApplicationContext::onCaptured);
  ```
- Use lambdas for simple forwarding, UI synchronization, or ad-hoc notification handlers.

---

## 4. SQLite & Data Storage Conventions

- **WAL Mode**: Every database instance must execute `PRAGMA journal_mode=WAL` and `PRAGMA synchronous=NORMAL`.
- **Foreign Keys**: Enforce relational integrity with `PRAGMA foreign_keys=ON` and `ON DELETE CASCADE`.
- **Parameter Binding**: NEVER construct queries via string concatenation. Always use `query.prepare()` and `query.bindValue()`:
  ```cpp
  QSqlQuery query(m_db);
  query.prepare(QStringLiteral("SELECT id, text_data FROM entries WHERE id = :id"));
  query.bindValue(QStringLiteral(":id"), entryId);
  ```
- **Deduplication Strategy**: Compute SHA-256 over `contentTypeTag(type) + '\0' + payload`. On duplicate hash, update timestamp and increment `use_count` rather than inserting a duplicate row.
- **Keyset Pagination**: For infinite scrolling in `ClipboardListModel`, use keyset cursors (`timestamp_ms` + `id`) to ensure \(O(1)\) page lookups regardless of dataset size.
- **Compaction**: Database `VACUUM` operations must run on a background worker thread (`VacuumWorker`) with an isolated database connection.

---

## 5. Platform Awareness (X11 vs. Wayland)

The codebase must support both X11 and Wayland Plasma sessions cleanly:

| Feature | X11 Implementation | Wayland Implementation |
| :--- | :--- | :--- |
| **Active Window Tracking** | `KWindowSystem` + `_NET_WM_PID` → process name | `wlr-foreign-toplevel-management` protocol → `app_id` |
| **Paste Simulation** | `libXtst` key event injection (fallback: `xdotool`) | Passive `KNotification` asking user to press `Ctrl+V` |
| **Global Shortcuts** | `KF6::GlobalAccel` (`Meta+V`, `Meta+Shift+V`) | `KF6::GlobalAccel` (`Meta+V`, `Meta+Shift+V`) |
| **Popup Positioning** | `QCursor::pos()` multi-monitor aware | `QCursor::pos()` multi-monitor aware |

---

## 6. UI & Presentation Guidelines

- **Native Plasma Look**: Do not hardcode custom color stylesheets (`setStyleSheet`) that break dark/light mode switching. Rely on `QPalette` and system Qt styles.
- **Themed Icons**: Load icons via `QIcon::fromTheme(QStringLiteral("icon-name"))`.
- **High-Performance Item Delegate**: `EntryDelegate` must keep item layouts lightweight, pre-cache group badge colors, and handle selection/hover rendering via `initStyleOption`.
- **Virtualized Lists**: Set `m_list->setUniformItemSizes(true)` and `m_list->setLayoutMode(QListView::Batched)` for smooth scrolling over tens of thousands of items.

---

## 7. Testing & Verification Requirements

- **Unit Testing Framework**: All tests reside in `tests/` using `Qt6::Test`.
- **Test Isolation**: Every test case must instantiate its own `QTemporaryDir` and isolated SQLite database to eliminate state leakage.
- **No GUI Dependency**: Core test executables must use `QTEST_GUILESS_MAIN(TestClassName)`.
- **Headless Smoke Test**: `main.cpp` must support the `--smoke` CLI flag to initialize storage, insert test entries, query them, and exit with code 0.

### Verification Protocol:
Before committing or marking any task complete, always verify:
```bash
cmake --build build
ctest --test-dir build --output-on-failure
./build/src/egoboard --smoke
```

---

## 8. Prohibited Practices & Anti-Patterns

1. ❌ **Do NOT introduce GUI/Widget dependencies into `src/core/`**.
2. ❌ **Do NOT use raw SQL string concatenation**; always use parameterized queries with `bindValue`.
3. ❌ **Do NOT inject synthetic keystrokes on Wayland** (Wayland security prohibits unprivileged client injection).
4. ❌ **Do NOT hardcode absolute file paths** for configuration or database files; use `QStandardPaths` and `SettingsManager`.
5. ❌ **Do NOT run SQLite `VACUUM` on the main GUI thread**.
6. ❌ **Do NOT use `get` prefix for getters** (use `name()` instead of `getName()`).
7. ❌ **Do NOT use C++ raw pointers with manual `delete` when `QObject` parentage or `std::unique_ptr` is suitable**.
