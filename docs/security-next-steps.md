# Security Next Steps

Leftover hardening items from the security audit (see `ROADMAP.md` for the
product queue). Ordered by risk. Items already delivered live in the git
history, not here:

- D-Bus same-UID auth + sensitive redaction (`EgoboardDbusAdaptor`)
- Owner-only DB/exports/backups (`0600` files, `0700` dirs, `NewOnly` backups)
- `PRAGMA key/rekey` hex blob literals, `secure_delete=ON`
- Wayland mime reads on a worker thread (generation-guarded, monotonic budget)
- SQLCipher capability ON + KWallet auto-provision + key rotation UI
- `includeSensitive=false` default on user exports (backups opt in)
- Icon/color-scheme traversal + symlink + display-name hardening
- Import/drag/regex/OCR/portal/token/pixel caps

## P0 — data disclosure

- [ ] **Split the D-Bus interface into read vs. write.** Replace
  `ExportAllSlots` with an explicit adaptor + XML (`Search/Preview` vs.
  `Paste/Copy/Pin/Delete`), rate-limit `Search`/`Preview`, and consider a
  caller allowlist beyond same-UID (e.g. KRunner only for automation).
- [ ] **Sanitize the HTML preview.** Untrusted clipboard HTML reaches
  `QTextBrowser::setHtml`: set `searchPaths({})`, `openLinks(false)` with
  click-to-confirm, `toHtmlEscaped()` all `sourceApp`/`sourceWindow` metadata,
  and re-validate `Files` paths at paste time (no `exists()` oracle at
  capture: treat `/`-leading text as text unless trusted `hasUrls()` says
  otherwise).
- [ ] **Encrypt backups when the DB is encrypted.** Backups are `0600` but
  still plaintext JSON; reuse the KWallet-held SQLCipher key (or warn +
  explicit opt-in) so an encrypted history does not leak through its backups.

## P1 — local robustness

- [ ] **Monotonic suppress/debounce clocks.** `suppressOwnSets()` and the
  data-control suppress window use wall-clock time; an NTP/manual jump
  stretches or collapses them. Move to `QElapsedTimer`/`steady_clock` and
  keep a single hash-based dedup gate for the dual QClipboard +
  data-control capture path.
- [ ] **Theme TOCTOU.** `exists()` → `openConfig()` and
  `isIconTheme()` → `themeGroup()` re-resolve the path; open by fd +
  `fstat` + canonical-prefix re-check instead.
- [ ] **`SystemThemeWatcher` fingerprint.** `mtime+size` collides and the
  config-change signal is unauthenticated: hash file contents, rate-limit
  `changed()` (~1/s token bucket), re-validate theme ids after every trigger.
- [ ] **Script actions trust boundary.** `actionsDir` `0700` + world-writable
  warning, per-script hash allowlist, 1 MB output cap, `matchPattern`
  validation (length ≤ 200, must compile) so a planted `.js` cannot persist
  a ReDoS pattern.
- [ ] **Snippet/transform caps.** `createSnippet` template ≤ 64 KB, `expand`
  output ≤ 1 MB, `TransformEngine::apply()` input ≤ 1 MB, entity-loop bound.

## P2 — platform / supply chain

- [ ] **X11 paste focus check.** Verify `XGetInputFocus()` before
  `XTestFakeKeyEvent`/`xdotool` injection so keystrokes cannot land in the
  wrong window after the 180 ms hide delay.
- [ ] **`SingleInstanceGuard` peer credentials.** `SO_PEERCRED`/`socketDescriptor`
  check on the local socket, nonce from the lockfile, socket under per-UID
  `RuntimeLocation` instead of a predictable `/tmp` name.
- [ ] **AppImage provenance.** Publish `APPIMAGETOOL_SHA256` with releases,
  build in a pinned container, gate vendored `.so` with `trivy`/`grype`,
  sign the AppImage (GPG) + `.sha256`.
- [ ] **Vendored Wayland XML provenance.** `third_party/protocols/README`
  with upstream URL + commit, `file(SHA256)` verify at configure time, prefer
  system `wayland-protocols` when available.
- [ ] **`CrashReport` invariant.** Keep `extraLogs` callers clipboard-free by
  construction (no payload strings in log lines that get bundled).
