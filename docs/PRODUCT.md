# Product definition

8bitShell is not a themed terminal and is not a collection of loosely connected utilities.
It is one workspace for operating remote systems.

## Product pillars

### Terminal quality

- Low-latency, GPU-backed rendering.
- Tabs, split panes, per-pane zoom, search, selection, hyperlinks, and profiles.
- ANSI color support plus opt-in keyword and regular-expression highlighting.
- Custom keyboard mappings and terminal behavior without rewriting the SSH protocol.

### Unified remote access

- SSH and local shells.
- SFTP with a queue, pause/resume, conflict handling, and remote editing.
- X11 forwarding on Linux using the host display stack.
- RDP through an isolated FreeRDP worker.
- A shared workspace model for terminal, file, and graphical sessions.

### Programmable connection behavior

- Aliases and inventory-backed target resolution.
- Organization profiles.
- Direct, jump-host, proxy, and privileged-access routes.
- Declarative username and destination transformation.
- Prompt-aware conversations for reason, account selection, MFA, and authentication.
- An explain view that shows how a request was resolved before it is executed.

### Operational logging

- Plain-text logs with predictable names and folders.
- Optional per-line timestamps.
- Append or overwrite behavior.
- Size-based rotation and retention.
- Automatic logging by global, profile, or session policy.
- A future searchable viewer without changing the portable log format.

### Team catalogs

- Human-readable files suitable for review and source control.
- Read-only shared catalogs with local overrides.
- Stable IDs so display names and folders can change without breaking references.
- No credentials in shared content.
- Import and export adapters for existing clients during migration.

## Visual direction

The default interface is compact and professional: high information density, restrained
motion, clear focus states, and strong terminal legibility. It has its own design system
rather than inheriting a GNOME, KDE, Windows, or web visual identity.

The visual system must support:

- reusable design tokens;
- light and dark themes;
- user-created terminal palettes;
- adjustable interface density;
- accessible contrast and focus indicators;
- high-DPI rendering;
- consistent appearance across supported desktops.

