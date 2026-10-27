# User guide

## Start a terminal

- Click the **+** beside the terminal tabs, or press `Ctrl+Shift+T`, for a local terminal.
- **New session** in the top-right opens the saved SSH session editor.
- Enter `host`, `user@host`, or `ssh` with the supported `-p`, `-l`, `-i`, `-J`, `-X`, `-C`
  options in Quick Connect. Remote commands, arbitrary options, and forwarding tunnels are
  rejected. For example: `ssh -p 2222 -J operator@bastion.example.test operator@db.example.test`.
- Press `Ctrl+K` to focus Quick Connect from anywhere.
- Saved session names also work in Quick Connect and retain their port, key, jump host,
  compression, and X11 preferences. Agent forwarding is blocked by policy.
- The search dropdown shows names, `user@host`, folder paths and tags. All query words
  must match; exact names/hosts rank first. Use Up/Down and Enter, or click a result.
  A query with no results never starts a connection by pressing Enter. For a raw target,
  click **Quick connect to:** at the bottom of the dropdown.

Tabs are real independent PTYs. Drag a tab to reorder it. Right-click a tab to pin it,
rename it, duplicate it, assign a color category, or close it/the tabs to either side/all
other tabs. Bulk close actions leave pinned tabs open. When the tab strip is full, use the
left/right buttons, a horizontal trackpad gesture, or the mouse wheel over the strip to reach
the rest. Selecting a tab always scrolls it back into view. Use `Ctrl+Tab` to switch and
`Ctrl+Shift+W` to close the active tab. `Ctrl++` and `Ctrl+-` change only that tab's terminal size.

The compact layout selector in the upper tab strip switches between **Tabs**, **2 panes**,
**4 panes** and **6 panes** without reconnecting sessions. The remaining width belongs to
the scrollable tab list, so many more tabs fit before overflow navigation is needed. Click a
pane to make it active; use page arrows for more sessions and drag the tabs to arrange their
order. **Multi SSH** sits in the lower action bar beside Macros and Monitor and opens an
optional command strip. Select targets, enter a command, then review and approve the exact recipients.
Normal typing and macros still affect only their individual terminal, not the selection.
See [Search, split panes and multi SSH](MULTI-SSH.md) before using broadcast commands.

## Save sessions

For CyberArk routing, folder connection rules, local employee identities, profile
pictures, and terminal registration, see [CyberArk and personal profile](CYBERARK.md).

The panel button beside **8bitShell**, left of Quick Connect, shows or hides the connection
browser. It stays visible when the browser is closed and its state is remembered. **Files**
in the top bar independently shows or hides the file browser and also remembers its state.
Highlight rules moved to **Settings → Rules** so the top bar stays focused on navigation.
Panel toggles do not disconnect or restart terminal sessions.

Use the **+** beside Connections, or **Add session** in the empty catalog. The folder path
accepts up to 64 levels and 1024 characters, for example `Production / Oracle / DC1`. Only connection
metadata is saved. Passwords, passphrases, MFA answers, and keyboard-interactive responses
remain inside the live OpenSSH terminal and are never written to the catalog.

The session editor's **Backspace sends** field can override the terminal default. Keep
`DEL · ^? · 0x7F` for ordinary modern Linux/readline targets. Choose `BS · ^H · 0x08` for
an Oracle or legacy program that displays `^?` or otherwise fails to erase. The active
choice is shown as `BKSP ^?` or `BKSP ^H` in the terminal status bar. This setting controls
the byte sent by the application; if the remote shell itself has a mismatched canonical TTY
setting, align it on that host with `stty erase '^?'` or `stty erase '^H'` as appropriate.
New sessions also start with a matching local PTY `VERASE` value so OpenSSH can forward the
choice as part of the remote pseudo-terminal request.

Click a folder once to expand or collapse it. The **⋮** button on every row opens the same
actions as right-click. Folders can create nested folders or sessions, be renamed, moved to
the root, or remove an empty branch. Sessions can connect, edit, duplicate, move to root, or
delete. Press `F2` while a row is focused to rename a folder or edit a session.

Drag a session onto any folder to move it there. Drag a folder to move its complete subtree,
including every nested folder and session. Drop either kind of row on the **Saved sessions**
heading to move it back to the catalog root. Invalid self/descendant moves and name
collisions are rejected without changing the catalog. Holding a drag near the top or bottom
of a long list scrolls it automatically. All successful moves are written immediately to
the readable catalog file.

**Expand all** and **Collapse all** are in the catalog menu, and expansion state is
remembered after restart.

Search covers the complete catalog—not only currently open folders—and checks folder paths,
session names, hosts, users, and tags. Matching branches open automatically while searching.
The list remains virtualized, so collapsed catalogs with hundreds of sessions do not create
hundreds of UI controls.

The catalog menu can import a complete nested mRemoteNG XML/`confCons` tree or concrete
`Host` aliases from `~/.ssh/config`. mRemoteNG folder depth, SSH name, host, user, and port
are retained; password and protected/encrypted fields are ignored. OpenSSH wildcard and
negated aliases are skipped.

## Connection security and first use

The small outline lock beside an open SSH tab's name (and a split pane's header) identifies
an app-managed SSH session. It is no longer shown in the saved-session catalog. Its tooltip
explains that host-key verification is required on connection. It is not a live authenticated
status, a certification badge, or a guarantee that the remote machine is safe. Local shells
and folders do not show this icon.

App-managed terminal/SFTP/monitor connections require a previously verified server key or
host CA. Unknown and changed keys fail closed; there is no “accept anything” switch. Obtain
the expected SHA-256 fingerprint or CA public key from your server administrator through an
independent trusted channel. Provision the matching `known_hosts` entry through your approved
process. Never trust a key merely because `ssh-keyscan` returned it. Non-default SSH ports
need the appropriate `[host]:port` entry; jump hosts need their own trusted entries too.

A changed key can mean legitimate rotation or interception. Stop, verify the rotation, and
update trust deliberately; do not blindly delete the old entry. Managed connections do not
auto-enroll or auto-rotate keys. Host CA entries and revocation markers are interpreted by
OpenSSH; CA administration is outside this app. The app never edits your trust store for you.

Managed connections use a private generated SSH configuration. They do not execute or inherit
`~/.ssh/config` or `/etc/ssh/ssh_config`, including `Include`, `Match exec`, `ProxyCommand`,
`LocalCommand`, distro crypto-policy includes, and user-defined multiplexing. Import concrete
aliases into saved profiles if needed. A non-default jump-host key may need to be loaded in
your local agent: the destination's `-i` is not a per-hop key configuration. Agent signing
for the current connection is supported; forwarding the agent socket to servers is not.

RSA keys below 3072 bits, SHA-1 KEX/signatures, CBC ciphers and legacy MACs are excluded.
Current OpenSSH modern defaults are otherwise retained. Old servers may need an administrator
upgrade. There is no automatic downgrade. Password and keyboard-interactive authentication
are available in the terminal after server trust verification; graphical SFTP is key/agent
only. This is not a FIPS-certified or OS crypto-policy-inheriting profile.

X11 is off by default. Enabling it shows a warning before connecting and only permits
untrusted `-X` forwarding with a 120-second authorization timeout for new X11 connections.
This timeout does not terminate already open graphical applications. Trusted `-Y` is blocked;
even `-X` relies on your local X server and should only be used with trusted hosts.

The local terminal intentionally remains a normal shell: `ssh` typed there is not wrapped
by this policy. This app is not an endpoint-wide SSH policy enforcement agent.

## Files: local and remote

**Files** opens a two-pane browser. The local pane also works in a local terminal, with
no SSH connection. In an SSH tab, click **Connect** for the remote pane; it uses the
profile's host, user, port, identity file, ProxyJump, and compression settings.

- Click a folder to enter it. Double-click also works without accidentally opening
  a second nested folder. **Ctrl+click** selects a folder for copying without opening
  it; right-click opens its menu. Dragging a folder does not navigate into it.
- With the file list focused, **Up / Down** select rows; **Enter / Right** open the
  selected folder. The **↓** toolbar button also opens it. **Alt+Up / Backspace**
  or **↑** go to the parent; **Alt+Left / Alt+Right** or **← / →** navigate history.
  **Home**, **↻ / F5**, and the editable path work in both panes.
- Drag one or several files from your desktop/file manager onto the remote list to upload
  into its current folder. Folders are transferred recursively.
- Select a remote item and click **↓ Copy** to copy into the local folder currently shown.
  **To…** lets you choose any writable local destination.
- Drag a local file out to a desktop/file manager, or into the remote pane.
- Drag a remote item toward a local destination. It is first downloaded to a private
  staging folder. Keep the mouse held until ready; for a large item, release, wait for
  **Drag ready**, then drag again. The native drag carries a standard file URL, so the
  destination file manager can copy it without needing its own SSH login.
- Open the destination directory before dropping. The two list areas are drop targets.
  A local drop copies; it never moves/deletes the source.
- Existing names visible in the destination are preserved by adding a numeric suffix.
  Remote uploads use the latest loaded directory listing; refresh before uploading into
  a directory another person is changing. A concurrent remote change is not an atomic
  conflict lock.
- File browsing is hidden on a fresh installation and remote SFTP never connects merely
  because the panel was opened. Use **Connect remote files** for the active SSH tab.
- Local and remote paths are remembered independently per open tab. If tab A is in one
  directory, switching A → B → A restores A's exact local and remote locations. Changing
  tabs during a transfer finishes that transfer on the original host before detaching the browser.

The SFTP path supports keys and local agents. Verified host keys/CA entries must be
provisioned before connecting, as described above. Password-only graphical SFTP still waits for the credential broker; ordinary
terminal password/MFA prompts are unchanged. SFTP navigation has a 30-second timeout;
a transfer has a 30-minute limit. Failed remote uploads can leave a partial new copy;
incomplete local downloads stay in private temporary staging, not at the final destination.
Control-character filenames are unsupported. Recursive operations reject symbolic links
and special files, including links nested inside an uploaded directory.

Remote drag snapshots now use private temporary directories, cleaned when replaced or when
the file controller is destroyed. Keep the app open until the receiving file manager finishes
copying. A crash may leave temporary files for OS/user cleanup. Old `drag-exports/` caches
from earlier builds were not deleted automatically; review those separately. Refresh/reconnect
before dragging a remotely edited file again. Local downloads are published with an atomic
no-replace operation; existing files and links are preserved even if a name appears mid-transfer.

## Macro buttons and folder inheritance

Use **Edit** on the button strip, the Connections menu, or a folder's
**Assign macro group…** action. Create a named group, then add buttons with a label and
literal, possibly multiline text. **Apply button** keeps an edit in the group editor.
**Save group** persists the group and current button; **Save & assign** also assigns it
to the folder selected at the top. The arrow buttons reorder macros.

**Send Enter after pasting** is OFF for new buttons. With it off, a single-line macro is
pasted for inspection; with it on, Enter is sent after the paste. Bracketed-paste framing
is used when the terminal application requests it. Multiline input, invisible formatting,
and Enter-sending macros now show a full-content review dialog with Cancel focused. Explicitly
approve only text you understand for the displayed destination. ESC/other terminal control
characters and oversized input are blocked. Bracketed paste does not make an arbitrary remote
program safe: a program may execute multiline input after you approve it.

The active terminal uses the nearest ancestor folder with a non-empty button group:

- `Oracle` assigned a group → every descendant inherits it.
- `Oracle/Production` assigned a different non-empty group → that subtree uses the override.
- Empty groups or **Inherit from parent** fall back to the parent; the root assignment is
  the final fallback.
- Renaming/moving a folder carries its assignments. Moving a saved session immediately
  resolves its new folder's group. No close/reopen is necessary.

The strip's selector can choose a group manually. Switching to another folder resets it
to automatic inheritance. **Macros** in the terminal's bottom bar shows/hides the strip;
the toggle remains available when the strip is closed.
Macros are readable YAML in `~/.config/8bitshell/macros.yaml`, not a credential vault.
The manager can import groups, export the selected group or all groups, and reload external
edits. Export saves the current edited group first. Imported ID conflicts become separate
copies; folder assignment import is optional and preserves existing assignments.
Never put passwords or tokens in macros. Invalid/unreadable macro files are preserved.

## Monitoring

Monitoring is OFF by default. Click **Monitor** in the terminal's bottom bar to show the compact
CPU, RAM, load averages, network download/upload rates, uptime, and the three or four mounted
filesystems using the most space. This intentionally samples filesystem usage rather than
recursively scanning directories such as `/home`, so it remains cheap on large hosts. **×**
stops it; the toggle is remembered separately for each open tab. Settings can opt new tabs in.

For remote Linux hosts, sampling uses a separate, non-interactive OpenSSH command channel
with key/agent authentication, every five seconds. Nothing is typed into the working
terminal and no monitoring output enters its session log. The first sample has no CPU
interval yet. Unsupported hosts or authentication failures show a status instead of
made-up readings. Monitoring a local shell samples this computer; complex Quick Connect
commands must be saved as profiles before they can be monitored remotely.

## Keyboard, mouse, and clipboard

Default copy/paste shortcuts are **Ctrl+Shift+C / Ctrl+Shift+V**.
Settings offers **Linux defaults** and **Insert keys** presets; the latter uses
**Ctrl+Insert / Shift+Insert**. **Paste primary selection** is separate
(default **Ctrl+Shift+Insert**) on platforms supporting a primary selection clipboard.

Click a shortcut field and press your combination. **×** unbinds it. Conflicts are
rejected; clear the old assignment before reusing it. Shortcuts and presets persist.

**Ctrl + mouse wheel** (including fractional trackpad movement) zooms only the current
terminal. Right-click opens copy/paste, select all, find, zoom, and scrollback actions.
Mouse-aware applications keep their own mouse events; hold **Shift** to select text or
open the terminal's menu instead.

**Copy selection automatically** is OFF by default. Enable it in Settings to copy after
releasing a mouse selection. With it off, selecting text does not replace the clipboard.

## Find, highlight, and log

- Press `Ctrl+F` to search the active terminal's live screen and scrollback.
- Toggle **HL** per tab. Open **Rules** to add, edit, prioritise, enable, or remove regular
  expression rules. Rules update open terminals immediately.
- Toggle **LOG/REC** per tab. Logs contain output with common ANSI/control strings removed.
  Keystrokes are not directly recorded, but remote echo/output can contain commands, passwords,
  tokens, database data, or authentication prompts. Logs are not automatically redacted.
- Click the profile control in the top-right and open **Logging** to control the log folder,
  file and subfolder templates, line timestamps, append/replace behavior, rotation size,
  and retention.

Supported log template tokens are `{session}`, `{host}`, `{user}`, `{date}`, and `{time}`.
New log files are owner-only. A log file cannot be used concurrently by two app instances;
choose unique names for simultaneous sessions. Rotation failure stops logging with an error
instead of silently truncating the current log. Set a finite rotation/retention policy for
long-running sessions. These ordinary logs are neither immutable audit records nor encrypted
storage; protect backups and the endpoint's disk accordingly.

## Terminal text rendering

In **Settings → Terminal**, **Text smoothing** offers System / auto (default), Grayscale,
and Sharp pixels (no smoothing). Grayscale avoids subpixel color fringes; sharp pixels
turns font antialiasing off. **Font hinting** separately offers system default, none,
light/vertical, and full pixel alignment. Changes apply to existing terminals and are
saved/exported in `settings.yaml`; no desktop-wide font setting changes.

Start with System / auto and default hinting. Try grayscale when small text has colored
edges, or light hinting if you prefer softer outlines. These are preferences: the font,
Qt backend, compositor and display scaling affect the result. Linux is not given a fake
“ClearType engine” toggle. ClearType is Microsoft's text-smoothing technology; Qt exposes
portable font antialiasing/hinting preferences instead. A separate renderer/backend selector
is not added without evidence that another backend is correct and faster.

References: [Qt QFont](https://doc.qt.io/qt-6/qfont.html),
[Microsoft ClearType](https://learn.microsoft.com/en-us/typography/cleartype/).

## Themes

Click the profile control in the top-right and choose **Appearance**. **Application theme** controls navigation,
dialogs, menus, and toolbars. **Terminal theme** independently controls the terminal
background, foreground, cursor, selection, and all 16 ANSI colors. Changes apply live to
open terminals and are remembered.

The appearance matrix has four independent slots: application day/night and terminal
day/night. Every slot accepts any palette, so a light application with a dark terminal
is supported. Existing theme selections are preserved during upgrades. A fresh installation
starts with Catppuccin Mocha for the night application/terminal pair and Catppuccin Latte
for the day application/terminal pair.

**Appearance mode** offers **Day**, **Night**, **Auto · follow OS**, or **Auto · schedule**.
OS mode follows the desktop's light/dark preference through Qt; if the platform reports no
preference, the app uses night themes and shows that status in Settings. Schedule mode uses
editable 24-hour local times (default day at 07:00, night at 19:00). Day starts inclusively;
night starts at its exact boundary. An overnight day interval is supported, and identical
start times keep night themes on all day. Clock/timezone changes and resume are re-evaluated.

The top-bar **Day / Night** button immediately switches both theme slots and selects manual
mode, so automatic updates cannot immediately undo the choice. Choose either Auto option
again to resume synchronization; the four saved palettes and schedule are retained.

The built-in set contains Blueberi (the revised original dark appearance), Dracula,
Catppuccin Mocha, Nord, Tokyo Night, Gruvbox Dark, Solarized Dark, One Dark, Monokai,
GitHub Dark, Ayu Mirage, Everforest, Rosé Pine, Kanagawa, and Material Ocean.
Six additional light palettes are available: Raspberi, Catppuccin Latte,
Solarized Light, GitHub Light, One Light, and Rosé Pine Dawn.

## Local files

Readable user configuration is kept under `~/.config/8bitshell/`:

- `sessions.yaml` — saved SSH sessions and explicit folder paths;
- `highlights.json` — custom render-only highlight rules;
- `macros.yaml` — macro groups, button text, and folder assignments;
- `settings.yaml` — UI theme, terminal theme, terminal defaults, and logging preferences.

The old JSON/INI files migrate on first launch and remain untouched. Export or reload through
**Settings → Configuration · YAML**, **Connections → … → Session YAML import / export…**,
or **Macro groups**. See [configuration and interchange](CONFIGURATION.md) for complete
formats, safety behavior, and importable examples.

Session logs default to `~/Documents/8bitShell/Logs/`. The app does not deliberately store
authentication answers, but echoed/output secrets can remain in logs and scrollback.
Macro text is stored verbatim; do not use it for secrets. Existing `blueberry` and
`blueberry-light` preference IDs are retained for compatibility and now display as
Blueberi and Raspberi.

## Behavior references

Macro groups and optional Enter follow the quick-command workflow described in the
[Xshell manual](https://www.netsarang.com/docs/Xshell7_manual.pdf). The keyboard presets,
primary selection, and Shift mouse override follow familiar
[Konsole conventions](https://docs.kde.org/trunk_kf6/en/konsole/konsole/commandreference.html).
Recursive transfers and batch semantics use [OpenSSH SFTP](https://man.openbsd.org/sftp);
external drags use [Qt file-URL drag and drop](https://doc.qt.io/qt-6.11/dnd.html).
