# Roadmap

Milestones are ordered by dependency and risk. They are not a reduction of the product scope.

## M0 — Foundation

- [x] Qt Quick application shell and design tokens
- [x] Connection request, target, and policy model
- [x] Deterministic route resolver and explain trace
- [x] Conversation state machine
- [x] Plain-text logger
- [x] Priority-aware highlight engine
- [x] Versioned configuration schemas
- [x] Build and visual verification on the development machine
- [x] Independent UI/terminal theme catalogs with Blueberi and Raspberi palettes
- [x] Day/night theme matrix, light palettes, OS/local-time auto switching, and quick toggle

## M1 — Real terminal and SSH

- [x] Reference libvterm terminal adapter and decision boundary
- [x] Linux PTY host
- [x] Interactive system OpenSSH path through Quick Connect
- [ ] Isolated OpenSSH worker
- [ ] Host-key confirmation UI
- [x] Native OpenSSH password, key, agent, and keyboard-interactive flows in terminal tabs
- [x] Reorderable/pinnable tabs, overflow navigation, tab actions, and terminal search
- [x] 2/4/6 split panes with page navigation
- [x] Per-terminal zoom, scrollback, selection, and clipboard
- [x] Global and per-session DEL/BS Backspace selection
- [x] Configurable shortcuts, Ctrl+wheel zoom, context menu, opt-in copy-on-select
- [x] Macro groups with nearest non-empty folder inheritance
- [x] Optional Linux host-monitoring strip with CPU/RAM/network/top-filesystem metrics
- [ ] Resolver explain UI and connection diagnostics
- [ ] Integrated-shell shim and `bitshellctl`

## M2 — Catalog and privileged access

- [x] Persistent human-readable YAML session catalog (automatic legacy JSON migration)
- [x] OpenSSH config importer
- [x] Arbitrarily nested, searchable, virtualized session tree
- [x] Persistent session/folder drag-and-drop with root moves and large-list auto-scroll
- [x] mRemoteNG XML tree importer with secret fields excluded
- [x] YAML sessions/settings/macros with typed validation, atomic writes, exact macro text round trips, and import/export
- [x] External-edit conflict protection and live reload; comments are reformatted on UI saves
- [ ] Personal and shared catalogs with overlays
- [ ] Inventory provider interface
- [ ] Full conversation actions, branching, timeouts, and retry policy
- [ ] Credential-provider interface
- [ ] Existing session-generator adapter
- [ ] MobaXterm and Xshell importers

## M3 — SFTP workspace

- [x] OpenSSH-backed remote file model
- [x] Upload, download, folder navigation, and collision-safe local names
- [ ] Queued transfers, progress, pause/resume, and conflict policy
- [x] Drag-and-drop upload
- [x] Per-tab local/remote browser state, both-pane navigation history, and explicit remote connect
- [x] File-URL drag exports and selectable local download destinations
- [ ] Native drag acceptance checks across KDE/GNOME/Windows file managers
- [ ] Automatic drag-cache retention/cleanup and transfer cancellation UI
- [ ] Remote editor hand-off and atomic upload
- [ ] File operation audit events
- [ ] Shared authentication where the selected SSH backend permits it

Current SFTP authentication is intentionally limited to SSH keys and agents until the
credential broker can present and isolate password/keyboard-interactive prompts.

## M4 — Graphical protocols

- [ ] Linux X11/Xwayland forwarding checks and lifecycle UI
- [ ] FreeRDP worker and embedded frame surface
- [ ] Clipboard and display-resize policy
- [ ] VNC worker evaluation

## M5 — Teams and Windows

- [ ] Signed team catalogs and policy packs
- [ ] Git, HTTPS, and shared-directory synchronization providers
- [ ] Conflict and review UI
- [x] Windows ConPTY adapter
- [x] Native Windows local host monitoring
- [ ] Windows credential and display providers
- [ ] Installer and update channels
