# Architecture

This document includes the target architecture, not only shipped components. For the
implemented Linux security boundary and test evidence, see [Security assessment](SECURITY-ASSESSMENT.md).
OpenSSH transport processes are separate today; libvterm and the UI still share a process.
The credential broker, isolated parser/plugin workers, and signed team synchronization below
are future work, not current security guarantees.

## Shape of the product

The user installs and opens one application. Protocol implementations and organization
extensions run behind it as isolated workers.

```text
bitshell (Qt Quick UI)
    |
    +-- connection-core
    |      +-- catalog
    |      +-- resolver and policy pipeline
    |      +-- conversation engine
    |      +-- credential broker interface
    |      +-- logging and highlighting
    |
    +-- terminal-engine adapter
    |      +-- Contour candidate
    |      +-- libghostty-vt candidate
    |
    +-- workers
           +-- ssh-openssh
           +-- ssh-managed
           +-- sftp
           +-- rdp
           +-- display
           +-- plugin-host
```

Workers will communicate using a versioned local IPC protocol. No protocol library ABI is
exposed directly to QML or to third-party extensions.

## Connection resolution

The [implemented PSMP routing](CYBERARK.md) separates local employee identities,
shared account rules, folder inheritance, and explicit direct-SSH exceptions.
It uses bounded structured fields, not the experimental template pipeline below.
Live organizational PSMP/MFA compatibility has not been tested.

Input is treated as structured data, not concatenated shell text.

```text
ssh db-prod-01
  -> parse request
  -> resolve catalog target
  -> select active organization policy
  -> expand route and username templates
  -> create conversation plan
  -> show explain trace when requested
  -> execute program + argument array
  -> attach terminal, logger, and optional file channel
```

Resolution fails closed when a required policy or variable is missing. Policy priority is
deterministic, and every applied decision is added to a human-readable trace.

## Session catalog and tree projection

The local catalog persists sessions as flat records with stable IDs and canonical
slash-separated folder paths. Explicit folders are stored separately so an empty folder does
not disappear. The UI requests a flattened projection containing only expanded branches;
Qt Quick's `ListView` then virtualizes that projection rather than instantiating every node.
Filtering is computed against the full catalog and includes the ancestry of every match, so
collapsed branches never hide a search result.

The mRemoteNG importer streams XML rather than building a second in-memory document. It maps
arbitrary container depth to canonical folder paths and reads only non-secret SSH metadata.
Password, protected payload, and encryption metadata are intentionally never copied.

## Integrated and external shells

Inside an application-managed terminal, an optional SSH shim can be placed first in that
terminal process's `PATH`. Managed targets go through the resolver; unmatched commands pass
through to the system OpenSSH client unchanged.

Outside the application, the same behavior will be exposed through `bitshellctl ssh TARGET`.
An opt-in shell function may provide the shorter `ssh TARGET` form. The system SSH binary and
global shell configuration are never replaced automatically.

## Terminal engine boundary

The product owns a small terminal-engine interface for input, output, screen snapshots,
selection, search, resize, render invalidation, and highlight decorations. A pinned terminal
engine sits behind that boundary.

The usable Linux adapter is built with `forkpty`, libvterm 0.3.3, and a native
`QQuickPaintedItem`. Every tab owns its PTY and terminal state. It provides the interactive
baseline while keeping terminal bytes, logging, search, and rendering separate. It is not
the final bake-off decision and does not change the planned worker boundary.

Terminal input has an explicit Backspace boundary. The global default and an optional
session override select either DEL (`0x7f`, `^?`) or BS (`0x08`, `^H`); Alt+Backspace adds
the normal ESC prefix. Before a child starts, the same choice is written to the local PTY's
POSIX `VERASE` slot. OpenSSH can then include the matching mode in its SSH `pty-req`, while
the selected key byte is still written unchanged to the PTY/OpenSSH process. The application
never injects `stty` commands into a remote shell.

Presentation is split the same way: UI tokens repaint the Qt Quick shell, while a terminal
scheme updates libvterm's default colors and indexed ANSI palette. Existing indexed cells
therefore repaint live without rewriting terminal output or log data.

The first technical bake-off compares Contour's `vtbackend`/`vtrasterizer` stack with
`libghostty-vt`. The decision is based on correctness and measured interaction latency rather
than project popularity.

Required corpus:

- `vttest` and representative escape-sequence tests;
- Unicode grapheme clusters, CJK, emoji, and combining characters;
- `vim`, `tmux`, curses mouse input, and alternate screen behavior;
- large and rapidly growing operational logs;
- high-DPI scaling and per-pane zoom;
- selection, copy, hyperlink, and search behavior.

## Syntax highlighting

Highlight rules operate on terminal cells after VT parsing but before rendering. They never
rewrite PTY bytes or the terminal grid. This preserves copy/paste, command behavior, and log
integrity.

Rules have stable IDs, priorities, enablement scopes, regular expressions, and visual styles.
When rules overlap, the higher-priority rule owns the overlap. The reference terminal adapter
maps UTF-16 matches back onto terminal cells at render time; copied text, PTY bytes, and logs
remain unchanged. The active rule catalog is persisted as readable JSON and pushed live to
open terminal views. Full grapheme-cluster validation remains part of the engine bake-off
corpus.

## SFTP

The graphical file panel invokes the system OpenSSH `sftp` executable with an argument
array and commands over standard input. It parses the SFTP long-list format into a Qt model.
Each navigation or transfer operation is isolated and finite; no shell command string is
executed. Remote and local paths are quoted for the SFTP command grammar.

The current implementation uses batch authentication so it can never hang on or capture an
unmanaged password prompt. It supports keys, an SSH agent, ports, ProxyJump, and compression.
Password and keyboard-interactive SFTP will be enabled only through the future credential
broker. Host-key verification is never disabled.

## Logging

The default logger writes ordinary UTF-8 text. ANSI control sequences are removed by a
streaming state machine, including sequences split between output chunks. Optional line
timestamps are presentation metadata in the file, not terminal input.

There is deliberately no input-logging API. OpenSSH handles authentication in the terminal;
the final credential broker is not implemented. Echoed input and remote output can still
contain secrets in logs/scrollback. There is no automatic redaction or immutable audit trail.

## Security boundaries

- Session and policy catalogs contain references, never secret values.
- Future credential providers will adapt to the operating-system store.
- Commands are executed as an executable plus argument list.
- Future external policy plugins must run out of process with a declared capability set.
- Host-key verification remains enabled and visible.
- Do not feed secrets into diagnostic traces, macros, or catalogs; remote output is not
  assumed to be redacted by the current terminal logger.
- Shared catalogs and plugins will support signatures before remote synchronization ships.

## Native Windows preview (September 2026)

The shared application now builds with MSVC/Qt on Windows 11 as well as Linux.
`Platform` selects trusted OS executable/configuration locations, `PtyProcess`
uses a native ConPTY backend on Windows and forkpty on Linux, and `PrivateFile`
uses Win32 handles/protected DACLs or POSIX file operations respectively. Connection
policy, catalogs, terminal rendering and QML are shared rather than forked.

Both platforms now use the same pinned, locally patched libvterm source, replacing
the earlier system-library choice. The patch and hostile-control regression tests
are part of the build. See [Windows support](WINDOWS.md) for the implemented
platform boundaries and the features still requiring acceptance testing.
