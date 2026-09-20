# Changelog

## 1.4.0 (2026-09-20)

### Added — encoding tools (second round)
- *Reload with Encoding...* (`win.reload-encoding`) — re-reads the file from
  disk and decodes it with a code page picked from a list (18 entries incl.
  Auto-detect). `notes_window_load_file_as()` takes the forced charset;
  `encoding_force()` builds the info block and strips a matching BOM.
  Warns before discarding unsaved changes; local files only.
- *Save As with Encoding...* (`win.save-as-encoding`) — the editor keeps
  UTF-8, the file on disk is written in the chosen encoding via
  `encoding_from_utf8()` (BOM written for the UTF-8/16/32 BOM variants).
  Characters the target encoding cannot represent abort the save with an
  explanatory dialog instead of being silently replaced.
- *Fix Mojibake* (`win.fix-mojibake`) — repairs text that was already UTF-8
  but got read once as a single-byte code page (`PrÃ­liÅ¡` → `Príliš`).
  `encoding_fix_mojibake()` re-encodes the text into each suspect code page
  and accepts the result only when it is valid UTF-8 end to end, contains
  multi-byte sequences, holds no control/unassigned characters, and the
  non-ASCII count drops (mojibake always inflates). Healthy text is left
  untouched.
- *Line Endings* submenu (`win.eol-lf`, `win.eol-crlf`) — rewrites every
  line ending and updates the status bar.
- *Next Non-ASCII by Kind* submenu (`win.encoding-next-class`, int target) —
  jumps to the next character of one class only, so a single hidden NBSP or
  zero-width character can be found without stepping through the accents.
- **Combining marks are a class of their own** (teal) and are highlighted
  together with the ASCII letter they sit on. A decomposed accent (NFD,
  e.g. `i` + U+0301) is a zero-width character: it was classified as a
  symbol and tagged, but the tag had no glyph cell to paint, so a file like
  `utf8-nfd.txt` reported 13 non-ASCII characters while showing no colour at
  all. `encoding_scan_nonascii()` now pulls the ASCII base character into
  such a run, and `NotesEncodingSpan.nonascii` keeps the honest character
  count separate from the highlighted extent, so the status bar and the
  Encoding Info window still report 13, not 26. The Encoding Info character
  list skips the base letter. *Convert to UTF-8* recomposes these into
  single characters, after which they highlight as ordinary accented Latin.
- The status bar shows the non-ASCII count while the highlight is on, e.g.
  `UTF-8 | TEXT | LF | 2,7 KB | 127 non-ASCII (7 invisible)`; a document
  with none reads `no non-ASCII`. Invisible characters are called out
  separately because they are the ones that cannot be spotted by eye. The
  count is recomputed on every edit through an idle callback
  (`notes_encoding_schedule_refresh()`), so rapid typing coalesces into one
  rescan instead of scanning the buffer per keystroke.
- Non-ASCII marks on the scrollbar overlay — `draw_scrollbar_markers()` now
  paints the encoding spans in their class colors on the left half of the
  overlay, search hits on the right half (overlay widened 6 → 8 px).

### Added
- Character encoding support (`encoding.c/h`, `encoding_view.c/h`)
  - Detection on load: BOM sniffing (UTF-8, UTF-16LE/BE, UTF-32LE/BE),
    pure ASCII, valid UTF-8, BOM-less UTF-16 (NUL position bias), then
    scored decoding against windows-1250/1251/1252, ISO-8859-1/2/15 and
    KOI8-R
  - Scoring prefers text-like output; non-ASCII-letter ratio picks Cyrillic
    over Central European code pages, absence of Latin Extended-A picks
    Western ones
  - Everything is converted to UTF-8 for the buffer; BOMs are stripped
  - Line-ending detection (LF / CRLF / CR / mixed) on the decoded text
  - Status bar now reads e.g. `windows-1250 → UTF-8 | TEXT | CRLF | 4.1 KB`
- Character classification inside an already-UTF-8 document — ASCII (1 byte)
  vs. accented Latin, typographic lookalikes (curly quotes, dashes, NBSP,
  ellipsis), invisible/control characters (zero-width, soft hyphen, BOM),
  other scripts (Cyrillic, Greek, ...) and symbols/emoji
- *Encoding Info...* — scrollable 620x620 window (`GtkWindow` +
  `AdwHeaderBar` + `GtkScrolledWindow`) with four sections:
  - **File** — path, local/remote, binary/truncated/dirty state
  - **Encoding** — detected encoding, confidence (and whether it came from a
    BOM or a heuristic), buffer encoding, what Save will write, line
    endings, character/line/byte counts
  - **Character classes and colors** — color chip + hex value, class name,
    count and run count, description and examples for all six classes
    (classes with no occurrences are dimmed)
  - **Non-ASCII characters** — up to 200 rows with Ln/Col, `U+XXXX`, the
    character on its class color and its UTF-8 byte length; clicking a row
    jumps to it in the text
- *Convert to UTF-8* (`win.encoding-convert`) — re-emits the whole document
  as UTF-8 NFC, strips stray U+FEFF, marks it dirty so the next save writes
  plain UTF-8 without a BOM, then *verifies* the result and reports it:
  `encoding_report()` validates the buffer and counts ASCII vs. multi-byte
  characters, max bytes per character, U+FFFD replacement characters,
  invisible characters and typographic lookalikes. The dialog states
  plainly that no other encoding is left, explains that non-ASCII
  characters are still UTF-8 (2-4 bytes), and lists what is worth cleaning
  up (lost bytes, invisibles, lookalikes, mixed line endings)
- *Replace Lookalikes with ASCII* (`win.encoding-asciify`) — turns curly
  quotes, dashes, NBSP and ellipses into their ASCII equivalents and drops
  zero-width / soft-hyphen characters
- *Highlight Non-ASCII Text* (`win.encoding-highlight`, stateful) — tags
  every non-ASCII run with a color per class (green = accented Latin,
  orange = lookalike, red = invisible, blue = other script,
  purple = symbol/emoji)
- *Next Non-ASCII* (`win.encoding-next`, Ctrl+Shift+E) — cycles through the
  runs, enabling the highlight on first use
- Mixed-encoding files (`encoding_detect` + `decode_mixed`)
  - A file that holds valid UTF-8 *and* bytes that cannot be UTF-8 at all
    (concatenated logs, a UTF-8 file appended to a windows-1250 one) is
    detected as `mixed: UTF-8 + <code page>` instead of being decoded end
    to end with a single code page, which used to turn the UTF-8 part into
    mojibake (`Príliš` → `PrГ­liЕЎ`)
  - Decoding walks the buffer stretch by stretch: valid UTF-8 sequences are
    copied through untouched, each run of foreign bytes is converted on its
    own with the winning code page
  - The code page is chosen by scoring the *mixed* decode of each candidate,
    so the foreign bytes are judged with their ASCII context intact
  - Guarded against binary noise (needs ≥ 2 valid multi-byte sequences and
    no NUL bytes in the first 8 KB)
  - The status bar shows just `mixed`; the code pages, byte split and
    stretch count are in the *Encoding Info...* window ("Mixed source" row)
    and in the *Convert to UTF-8* report
- `save_text_utf8()` in `actions_file.c` — every local save (Save and
  Save As) goes through one function that validates the text with
  `g_utf8_validate` before writing, writes `strlen` bytes with `fwrite`
  (the old `fputs` path was fine but stopped at a NUL) and never emits a
  BOM. A failed validation refuses the save instead of writing a broken
  file.
- `notes_encoding_mark_utf8()` — single place that records "this document is
  now plain UTF-8", used by Save, Save As, New and Convert
- Same detection path for remote (SFTP) files

### Changed — one document, faster open
- `on_open` opens **one** file: it reuses the existing window instead of
  creating one per argument, and reports the ignored extras on stderr. One
  document per window was the intent; the loop contradicted it.
- Replacing the document now asks when there are unsaved changes
  (`notes_window_request_open()`), and *Open File* / *Open Recent* go
  through the same path — until now Ctrl+O silently discarded them.
- Starting with a file argument no longer loads the previously open file
  first. `notes_window_new()` no longer restores `last_file`;
  `notes_window_restore_last()` is called only from `on_activate`. A start
  with a file used to load two whole documents and throw the first away.

### Performance of opening a file (5 MB samples, measured)
Total went from ~105 ms to ~80 ms, with the double load above removed on top
of that:
- `notes_set_original_take()` takes ownership of the decoded buffer instead
  of copying it, reuses the hash already computed for the disk comparison
  when nothing had to be transcoded, and takes the character count from the
  buffer (O(1)) instead of walking the text: **24 ms → 0.1 ms**.
- The redundant `g_utf8_validate()` over the whole buffer is gone: the
  detector already validated the UTF-8/ASCII case, and every conversion path
  produces valid UTF-8 by construction (`known_valid`): **6 ms → 0 ms**.
- Line-ending detection stops after 1 MB instead of scanning everything:
  **7 ms → 1 ms**.
- Encoding detection samples 32 KB instead of 64 KB (same results on every
  test sample).
- `gtk_text_buffer_set_text()` runs inside
  `begin/end_irreversible_action()` and with the syntax engine switched off,
  so the load is neither recorded as an undo step nor highlighted up front.
- What remains is `set_text` itself (~50-78 ms), which is GtkTextBuffer's
  own cost, plus the disk read and hash (~7 ms).

### Fixed — audit round (moderate)
- Notification dialogs no longer leak. `GtkAlertDialog` keeps its object and
  strings until the user answers, so closing the window instead of pressing
  the button leaked them (measured under ASan, ~1 KB per dialog). Replaced
  with a small owned `GtkWindow` + `AdwHeaderBar`, which also matches the
  theming every other dialog here uses. ASan now reports zero leaks in our
  code across all action groups.
- *Browse* in the SFTP dialog leaked a `GtkFileDialog` per click and used
  `SftpCtx` without a reference — closing the SFTP dialog while the chooser
  was open could touch freed memory. Both fixed.
- `GtkFileDialog` async calls now follow the GTK 4 pattern: unref right
  after the call, never unref the borrowed source object in the callback.
- `theme.c` leaked the language-spec search path strings
  (`g_ptr_array_new()` with `g_strdup`ed elements). Verified under ASan that
  `GtkSourceLanguageManager` copies them, so freeing ours is safe.
- `settings_save()` wrote through a predictable `settings.conf.tmp` with
  `O_CREAT|O_TRUNC` (a symlink planted there would be followed). Now uses
  `g_mkstemp()` + `fchmod(0600)` like `connections_save()`, matching what
  the docs already claimed.
- The file monitor re-read the **whole** file on every change event and
  compared it against `original_hash` — which covers the *decoded* text, so
  for any non-UTF-8 file the comparison never matched and the editor kept
  reloading after its own saves. It now hashes at most `MAX_DISPLAY_BYTES`
  of raw bytes and compares with a new `disk_hash` recorded on load and save.
- Dirty detection no longer copies and hashes the entire buffer on every
  keystroke: a differing character count (O(1)) settles the common case, and
  the hash plus `strcmp` run only when the lengths match.
- Search match offsets went stale as soon as the buffer was edited, so Enter
  could jump to a shifted position. The highlight is refreshed on every edit
  while the bar is open, guarded by `search_busy` so Replace All stays O(n)
  instead of O(n²).
- `ssh_to_remote_path()` read past the end of the string when the local path
  did not start with the mount prefix; it now checks with `g_str_has_prefix`.
- `settings_load()` zeroes the struct first instead of relying on the caller
  (`recent_count` was read before being set).
- Removed the SFTP password field: every ssh call runs with `BatchMode=yes`,
  which disables password authentication, so the field could never do
  anything. Replaced with a note that ssh-agent is used when no key is given.
- Remote paths were built into fixed 4 KB buffers; a long path was silently
  truncated, which could open or later overwrite a *different* file. Built on
  the heap now. This also clears the last two compiler warnings — the build
  is warning-free.
- `original_content` bookkeeping moved into one `notes_set_original()`
  helper (it was duplicated across seven call sites, each re-deriving the
  hash).

### Fixed — audit round (serious)
- **Closing with unsaved changes on a remote file silently lost them.**
  `auto_save_current()` wrote to the virtual mount path
  (`/tmp/note-light-sftp-PID-user@host/...`), which never exists on disk:
  `g_mkstemp()` failed, nothing was written, yet the dirty flag was cleared
  and the window closed. It now routes remote documents through
  `save_remote_file()`, uses the shared validated writer
  (`notes_save_text_utf8()`) for local ones, returns a status, and the
  close handler keeps the window open with an explanation when the write
  fails.
- **Use-after-free in the remote file browser.** The directory listing ran
  in a thread with `OpenRemoteCtx` as callback data, but the context was
  freed when the dialog was destroyed — closing the browser while `ls` was
  in flight ran `browse_done()` on freed memory. `OpenRemoteCtx` now has
  the same `ref_count` + `dialog_alive` guard as `SftpCtx`.
- **A too-large remote file could be written back over the original.**
  `ssh_cat_file()` returned `"(file too large)"` as if it were content,
  with `is_truncated = FALSE`. It now reports the condition through a new
  `out_too_large` argument, and the caller marks the document truncated, so
  Save redirects to Save As and the remote file cannot be replaced by the
  placeholder. The status bar says "too large to load (read-only)".
- **Remote paths were pasted into the remote login shell unquoted.** `ssh`
  concatenates the remote command, so a file or directory named
  `a; touch INJECTED; echo x` executed on the server. `cat`, the directory
  listing and the write path now quote every path with `g_shell_quote()`.
  Verified: the old `tee` form creates `INJECTED`, the new one writes the
  oddly-named file correctly and creates nothing.
- **Remote saves were not atomic.** `tee file` truncates before the data
  arrives, so a dropped connection left a half-written remote file. The new
  `ssh_remote_write_command()` copies the permissions to a temporary next
  to the target (`cp -p`), writes it, and renames over the original,
  cleaning up on failure. Verified locally: content replaced, mode 640
  preserved, no leftovers; on a read-only directory the command fails and
  the original is untouched.

### Fixed
- Use-after-free in the encoding picker dialog: the dialog owned the
  `PickCtx` and freed it on destroy, so a second `row-activated` (a double
  click, or Enter arriving right after a click) ran the handler on freed
  memory — it crashed or acted on a garbage `NotesWindow`, which is what
  made a later *Reload with Encoding...* answer "This document has no file
  on disk yet". The handler now disconnects itself before destroying the
  dialog. Verified: without the fix a double activation segfaults, with it
  three rounds of double clicks are clean.
- *Reload with Encoding...* keeps the caret line/column, and never passes
  `win->current_file` as the path to `notes_window_load_file_as()` (which
  would `snprintf` a buffer into itself).
- `Makefile` had no header dependencies, so editing a `.h` left stale object
  files behind (mismatched struct layouts). Added `-MMD -MP` and
  `-include $(OBJ:.o=.d)`.

### Changed
- Non-UTF-8 files are no longer flagged binary/read-only; they are decoded
  and stay editable. Only embedded NUL bytes (or text that survives no
  decoder) mark a file as binary.
- Saving always writes UTF-8 and resets the status bar encoding

## 1.3.0 (2026-05-05)

### Added
- Show / hide whitespace toggle in hamburger menu (`win.toggle-whitespace`,
  stateful action) — renders spaces, tabs, newlines and NBSP via
  `GtkSourceSpaceDrawer`; persisted in settings.conf as `show_whitespace`
- Auto-reload when the open file changes on disk
  - `GFileMonitor` per window, installed in `notes_window_load_file`
  - Skips events caused by our own atomic save (FNV-1a hash compare against
    `original_hash`)
  - Clean buffer → silent reload preserving cursor line/column
  - Dirty buffer → modal dialog (Reload (discard) / Keep my version)
  - Skipped for SSH/remote files
- Undo / Redo
  - Menu items in Edit section
  - Accelerators Ctrl+Z, Ctrl+Shift+Z, Ctrl+Y
  - Backed by built-in `gtk_text_buffer_undo` / `_redo`
- Print support
  - `Ctrl+P`, *Print...* in menu
  - `GtkPrintOperation` with `PangoLayout` pagination
  - Uses the configured monospace font; word-char wrap to page width
- Recent files
  - *Open Recent* submenu (last 10), dynamically rebuilt after each load
  - Persisted as `recent_N=...` in settings.conf
  - Dedup + push-front on each successful open
  - `win.open-recent` action takes a path string parameter
- Ctrl + mouse wheel zoom on the text view (1pt step, clamped 6..72)
- Hamburger menu re-organized into File / Edit / View / SSH / Settings
  sections (visual separators between groups)

### Changed (refactor)
- Split monolithic `window.c` (~1600 lines) into focused modules:
  - `theme.c/h` — custom themes, CSS, source style/language, dark detection
  - `editor_view.c/h` — `NotesTextView` subclass, line numbers, font
    intensity, dirty detection, buffer signal wiring, whitespace drawer,
    Ctrl+scroll zoom
  - `search.c/h` — find / replace bar, scrollbar markers, Go to Line dialog
  - `ssh_window.c/h` — connect / disconnect, open / save remote, status
    button, SSH action gating
  - `window.c` retains: `notes_window_new`, `load_file`, close/destroy,
    auto-save, file monitor handler, print, recent menu rebuild
- Split `actions.c` (~1190 lines) into:
  - `actions_file.c` — new / open / save / save-as / open-recent / print
  - `actions_view.c` — find / find-replace / goto-line / zoom / settings
    dialog / undo / redo
  - `actions_ssh.c` — SFTP dialog + remote file browser
  - `actions.c` — only `actions_setup` (action map + accelerators) +
    `toggle-whitespace`
  - Cross-file handler declarations live in `actions_internal.h`

## 1.2.1 (2026-04-15)

### Performance
- Dirty detection uses FNV-1a hash — avoids O(n) strcmp on every keystroke; full comparison only when hashes match
- Search navigation uses stored byte offsets — O(1) jump to any match instead of O(n*k) re-scan from buffer start
- Remote file browser (ls) runs async via GTask — no longer blocks the UI while listing directories

### Fixed
- Use-after-free in SFTP connect dialog when dialog closes during in-flight async SSH connection (ref-counted SftpCtx with dialog_alive flag)
- GotoData memory leak when Go to Line dialog is closed without pressing Enter (freed via g_object_set_data_full on dialog)
- match_offsets array freed in search_clear_matches and on_destroy

### Security
- Atomic file writes use g_mkstemp() (exclusive O_EXCL creation) instead of predictable .tmp suffix — prevents symlink attacks on temp files
- connections_save() now uses atomic write (mkstemp + rename) — crash during save no longer corrupts the file

## 1.2.0 (2026-04-15)

### Added
- Syntax highlighting via GtkSourceView 5 with auto language detection from filename
- Syntax Highlight toggle in Settings dialog (on by default, persisted in settings.conf)
- Theme-aware syntax color schemes — each app theme maps to a matching GtkSourceView style scheme (solarized→solarized, monokai→oblivion, nord→cobalt, gruvbox→classic-dark/kate, etc.)
- Custom Assembly language definition (data/language-specs/asm.lang) with x86/ARM registers, instructions, NASM/MASM/GAS directives
- Makefile highlighting support (built-in GtkSourceView language + basename fallback detection)
- GtkSourceView 5 dependency added to Makefile

### Changed
- Text view is now a custom subclass of GtkSourceView instead of GtkTextView
- GtkSourceBuffer replaces GtkTextBuffer (with backward-compatible aliases)
- Language detection and style scheme applied before text is loaded into buffer (fixes highlight missing on startup file restore)
- CSS no longer sets text foreground color when syntax highlighting is on (style scheme controls it)
- Font intensity uses CSS opacity instead of foreground text tag when syntax highlighting is on (preserves syntax colors)

## 1.1.0 (2026-04-15)

### Added
- Find (Ctrl+F) with match highlighting, match count, next/prev (Enter/Shift+Enter)
- Find & Replace (Ctrl+H) with replace one and replace all
- Scrollbar match markers (orange indicators on vertical scrollbar showing match positions)
- Go to Line (Ctrl+G)
- SSH/SFTP remote file editing via system OpenSSH client
  - SFTP Connect dialog with saved connection profiles (host, port, user, key, remote path)
  - Connection profiles persisted in ~/.config/notes-light/connections.conf (0600 permissions)
  - Async connection test via GTask (non-blocking UI)
  - SSH ControlMaster for connection multiplexing (near-zero per-command overhead)
  - Remote file browser dialog (ls -1pA, directory navigation, click to open)
  - Remote file open (ssh cat)
  - Remote file save (ssh tee) — Ctrl+S works for remote files
  - SSH status indicator in status bar
  - Auto-disconnect on window close
  - Remote paths not saved as last_file
- AdwHeaderBar on all dialog windows (Settings, SFTP, Open Remote, Go to Line)
- Full theme CSS for dialog widgets (entry, label, list, button, separator, scrolledwindow, checkbutton, scale)
- Find, Find & Replace, Go to Line in hamburger menu
- Save confirmation dialog on close with unsaved changes (Save / Don't Save / Cancel)
- Smart dirty detection — undo back to original content clears dirty flag and removes `*` from title
- "Open Remote File" and "SFTP Disconnect" menu items disabled when not connected
- Application icon (SVG + PNG) and .desktop launcher

### Fixed
- Font intensity now applies to entire buffer (was only visible range + margin, causing 100% intensity on scroll)
- Font intensity now re-applied after file load (was lost because apply_settings ran before load_file replaced buffer content)

## 1.0.0 (2026-04-14)

Initial release. Stripped-down fork of notes-desktop.

### Added
- Plain text editor with GTK 4 + libadwaita
- 13 color themes (System, Light, Dark, Solarized Light/Dark, Monokai, Gruvbox Light/Dark, Nord, Dracula, Tokyo Night, Catppuccin Latte/Mocha)
- Custom GtkTextView subclass with current line highlight overlay
- Line numbers via Cairo GtkDrawingArea (visible lines only)
- Font intensity control (alpha blending)
- Configurable line spacing and word wrap
- Zoom in/out (Ctrl+/-)
- Status bar: encoding, file type (TEXT/BIN), file size, cursor position (Ln/Col)
- Open any file type including binaries (NUL→'.', non-UTF-8 converted via ISO-8859-1 fallback)
- 5 MB display cap for large files (keeps UI responsive)
- Open File (Ctrl+O), Save (Ctrl+S), Save As (Ctrl+Shift+S), New (Ctrl+N)
- Settings dialog: theme, editor font, GUI font, font intensity, line spacing, line numbers, highlight line, word wrap
- Persistent settings in ~/.config/notes-light/settings.conf
- Auto-restore last opened file on startup
- Auto-save on close (text files only)
- Atomic file writes (tmp + rename) for content and settings
- Signal blocking during file load (prevents dirty state/title corruption)
- Title restore via idle callback after gtk_window_present
- CSS injection protection (font name sanitization)
- Binary/truncated file protection (Save redirects to Save As)
- Idle source cleanup on destroy (prevents use-after-free)

### Removed (vs notes-desktop)
- Markdown rendering (WebKitGTK)
- SQLite FTS5 database
- Sidebar with search
- PDF export (Poppler + Cairo)
- Syntax highlighting (re-added in 1.2.0 via GtkSourceView 5)
- Pack notes (archive)
- Markdown preview
