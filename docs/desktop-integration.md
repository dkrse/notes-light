# Desktop Integration

How to register Notes Light with the system (application menu, desktop icon, default handler for text files) on Linux distributions using `xdg` (GNOME, KDE, XFCE, Cinnamon…).

## 1. Binary

The compiled binary must exist first:

```bash
cd ~/Apps/notes-light
make
```

Output: `~/Apps/notes-light/build/notes-light`.

The application supports opening files via command-line arguments:

```bash
./build/notes-light file.txt
```

It uses `G_APPLICATION_HANDLES_OPEN` — each file passed on the command line opens in its own window.

## 2. Desktop icon

Create `~/Desktop/notes-light.desktop`:

```ini
[Desktop Entry]
Type=Application
Name=Notes Light
Comment=Notes Light editor
Exec=/home/USER/Apps/notes-light/build/notes-light %F
Icon=/home/USER/Apps/notes-light/images/notes-light.png
Terminal=false
Categories=Utility;TextEditor;
StartupNotify=true
StartupWMClass=com.notes.light
```

`StartupWMClass` must match the application ID passed to `adw_application_new()` in `src/main.c` (`com.notes.light`). Without it, GNOME Shell cannot link the running window's `app_id` to this `.desktop` file, so the app appears in the dash as a separate, unnamed icon instead of highlighting its launcher.

Make it executable and trusted (GNOME/Nautilus refuses to launch it otherwise):

```bash
chmod +x ~/Desktop/notes-light.desktop
gio set ~/Desktop/notes-light.desktop metadata::trusted true
```

If the icon still shows as "untrusted", right-click → **Allow Launching**.

## 3. Adding to the system menu

Copy (or create) the same `.desktop` file into the user-level applications directory:

```bash
cp ~/Desktop/notes-light.desktop ~/.local/share/applications/
update-desktop-database ~/.local/share/applications/
```

From now on Notes Light appears in Activities / Application menu and is visible to other tools (`xdg-open`, "Open With…", `gtk-launch`, …).

System-wide installation (for all users) goes into `/usr/share/applications/` — requires `sudo`.

## 4. Associating with file types

To make Notes Light offered as a handler for text files, add a `MimeType=` line with a list of MIME types to the `.desktop` file:

```ini
MimeType=text/plain;text/markdown;text/x-markdown;text/x-log;text/x-readme;application/x-shellscript;text/x-c;text/x-csrc;text/x-python;text/x-script;text/x-tex;text/csv;application/json;application/xml;text/html;text/css;
```

After editing, refresh the database:

```bash
update-desktop-database ~/.local/share/applications/
```

### Setting as default

Per MIME type via CLI:

```bash
xdg-mime default notes-light.desktop text/plain
xdg-mime default notes-light.desktop text/markdown
xdg-mime default notes-light.desktop application/json
```

Check which app is default for a given MIME type:

```bash
xdg-mime query default text/plain
```

Detect the MIME type of a specific file:

```bash
xdg-mime query filetype file.txt
```

### Alternative via file manager

Right-click a file → **Properties** → **Open With** → select **Notes Light** → **Set as Default**.

## 5. Verification

```bash
gtk-launch notes-light file.txt
xdg-open file.txt
```

Both commands should open Notes Light with the given file.

## 6. Uninstall

```bash
rm ~/.local/share/applications/notes-light.desktop
rm ~/Desktop/notes-light.desktop
update-desktop-database ~/.local/share/applications/
```

You may also want to clean up `~/.config/mimeapps.list` — user-level default associations are stored there.

## Key `.desktop` fields

| Field | Meaning |
|-------|---------|
| `Exec` | Command to run. `%F` = list of local files, `%f` = single file, `%U`/`%u` = URI variants. |
| `Icon` | Absolute path to a PNG/SVG or a named icon from the current theme. |
| `MimeType` | Semicolon-separated list of MIME types the app can open. Must end with `;`. |
| `Categories` | Where it lands in the menu (`TextEditor`, `Utility`, `Development`…). |
| `Terminal` | `false` for GUI apps. |
| `StartupNotify` | Shows the "loading" cursor during launch. |
| `StartupWMClass` | Must equal the GApplication ID (`com.notes.light`) so the dash groups the window with its launcher icon. |

Specification: <https://specifications.freedesktop.org/desktop-entry-spec/latest/>
