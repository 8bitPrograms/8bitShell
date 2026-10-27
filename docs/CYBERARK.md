# CyberArk PSMP, personal profile, and Linux terminal registration

Implemented on Linux. No live CyberArk, corporate network, MFA, or Windows validation
has been performed or is required for this development stage. Offline routing tests
do not establish compatibility with a particular organization's PSMP policy.

## Configure once, share without personal identities

1. Click your avatar/initial and name in the top-right, then use **Profile** for the
   ordinary display name shown by 8bitShell.
2. Open **Settings → Authorization** and enable **CyberArk routing**. The Employee ID,
   route, approval, and dedicated-key controls stay completely hidden until this master
   switch is enabled. Save the Employee ID exactly as required; casing is preserved.
3. **Authorization → Connection routes** creates/edits named PSMP routes. Set the
   gateway (for example `psmp.example.test`), port, and default account rule.
   Choose personal username + `SYS` and your account domain, or a fixed account.
4. In **Authorization → SSH keys**, optionally assign one managed key as the dedicated CyberArk
   key. Replacing its daily private-key file retains the stable assignment. If no dedicated
   key is selected, the session/application key behavior remains available.
5. A folder's context menu **Connection rule…** assigns a route. In each session's editor,
   tick **Use CyberArk routing** for an explicit PSMP connection. **Advanced** still exposes
   **Inherit from folder**, **Direct SSH**, and **CyberArk route** for full control.
   Override the target account for exceptions such as `opc` with an empty domain.
6. New/changed route definitions require a local review before the app sends an
   identity. This consent does **not** accept SSH host keys; existing verified
   `known_hosts`/CA requirements still apply. Forget approvals under **Authorization**.

The session's host is the real target. The gateway belongs to the route. With an
employee username `EMP000123`, the two supported forms are:

- `EMP000123@EMP000123SYS%corp.example.test@db01.example.test`
- `EMP000123@opc@cloud-db02.example.test`

The same shared catalog resolves using each colleague's local username. The nearest
explicit ancestor assignment wins. An explicit direct session/folder bypasses
inheritance and uses the ordinary session `user`. Missing referenced routes or
personal identity block connection, never fall back to direct SSH. Account overrides
replace the whole account rule, so a fixed `opc` does not inherit a SYS suffix/domain.
Inherited sessions with no ancestor assignment remain direct unless an account
override requires a PSMP route. Pre-existing sessions lacking `connection` stay direct.

Use the importable [example catalog](../examples/config/cyberark-sessions.yaml),
replacing its fictional targets/domain with approved values. Current sessions export
as version 2; version 1 is still accepted. The older `examples/catalog/policies.yaml`
remains an unconnected, experimental general-purpose template/conversation design.

Runtime YAML additions in `sessions.yaml`:

- `routes`: named definitions with `type: cyberark-psmp`, `host`, optional `port`,
  `account`, and optional `allowSftp` / `allowMonitoring` (both false by default).
- `account`: `{source: identity, suffix: SYS, domain: corp.example.test}` or
  `{source: literal, name: opc}`. Domain is optional; there are no scripts/templates.
- `folderConnections`: canonical folder paths mapped to `{mode: route, route: id}`
  or `{mode: direct}`. Assign inheritance in the UI to remove an explicit folder rule.
  Folder paths may also be implied by a session's folder or a listed subfolder.
  An inherited folder assignment cannot carry an account override; select a route
  explicitly on that folder when overriding its target account.
- Session `connection`: `{mode: inherit}`, `{mode: direct}`, or
  `{mode: route, route: id}`, optionally with a full `account` override for PSMP.

Folder moves/renames remap assignments transactionally; removing empty folders
removes their rules. Imports reject conflicting route IDs/folder assignments instead
of overwriting them. A new imported assignment that would affect existing inherited
sessions is blocked; import into a distinct folder or assign the rule explicitly.
Deleting a route preserves session references so missing routes remain visibly blocked.

## Execution boundary and limits

`SessionStore.prepareProfile` calls `ConnectionRouting` once when a saved session is
opened (also via Quick Connect, including `ssh saved-name`). The validated result is
snapshotted in that tab. Terminal, SFTP, and monitoring use the same snapshot. Existing
tabs keep their launch identity, target, and route when catalog/personal data changes;
open a new tab to pick up route changes. Restart uses the frozen route plan and rechecks
local route approval, current SSH policy, and the current managed key generation; a daily
key replacement therefore applies to the next connection/restart without editing sessions.

`SshSecurity` validates the structured PSMP components before formatting and requires
a currently approved route fingerprint before launching any transport. The `%` is
allowed only in this validated formatter; direct username/key-path restrictions are
not loosened. Terminal/monitoring SSH receive `-l` and the gateway as separate argv.
SFTP receives one `[composite-user]@gateway` destination argument, never a shell string.
All retain the private SSH configuration and host-key/crypto/forwarding restrictions.
When a concrete key is selected, all transports receive the same `-i` path plus
`IdentitiesOnly=yes`; unrelated agent keys are not offered. A missing/unsafe selected key
blocks authentication instead of attempting a different route or identity.

This formatter currently supports DNS/IPv4 targets on port 22. Gateway ports are
configurable. Non-default target ports, IPv6 targets, extra ProxyJump, arbitrary PSMP
grammars, or scripted conversation responses are blocked/not implemented rather than
guessed. Authentication prompts remain in OpenSSH's interactive terminal. Nothing is
automatically substituted into commands typed in a local shell.

The client verifies the gateway's host key, not the final server's key independently;
target-side verification is the organization's CyberArk responsibility. The UI and
transport status distinguish destination from gateway. No password/MFA value is put
in YAML, approvals, or command arguments. Terminal output/logs still may contain
remote-echoed secrets; this is not a credential broker or automatic log redactor.

SFTP and monitoring require explicit route capability opt-in and key/agent batch
authentication supported by the organization. They never bypass the gateway or reuse
terminal passwords. Their availability is not implied by successful terminal MFA.
Monitoring stays off by default. No live PSMP protocol support is claimed.

## Local profile picture

`personal.yaml` (kind `8bitshell/personal`, version 1) stores the ordinary `displayName`,
`cyberArkEnabled` (default false), CyberArk-only `employeeId`, `avatarEnabled` (default
false), and local `approvedRoutes` fingerprints. A legacy `username` field migrates to
`employeeId`. The file is separate from session/settings exports and cannot be overwritten
by their export controls.
It is personal local configuration, not a credential vault.

**Choose picture…** accepts a local PNG/JPEG up to 8 MiB / 4096 × 4096 pixels. The app
decodes bounded input and writes a fresh, at-most-256-pixel PNG without source metadata
to its own private `avatar.png`. Original paths/files are not retained or changed.
Remote URLs, SVG/animation formats, symlinks and special files are rejected. Choosing
a file does not enable it: tick **Show profile picture instead of initial** separately.
Without that opt-in (or on a load error), the initial is shown. **Remove copy** deletes
only the app-owned image. This avatar is local and is not included in shared catalogs.

## Linux registration, not a default takeover

Installation advertises the `TerminalEmulator` desktop category while keeping
`Terminal=false` (the app provides its own terminal UI). It also supplies
`X-TerminalArgExec=-e`, `X-TerminalArgDir=--working-directory`, and
`X-TerminalArgTitle=--title` for implementations of the proposed
[XDG default-terminal specification](https://github.com/Vladimir-csp/xdg-terminal-exec).
Terminal selection is a desktop category/invocation contract, not a generic
`application/terminal` MIME type. No fictional MIME type, SSH URL handler, default
terminal preference, global shell override, or `mimeapps.list` change is installed.

```sh
bitshell --working-directory /tmp -e /usr/bin/printf '%s\n' 'one literal argument'
```

`-e`, `--execute`, or `--` consumes the rest of argv verbatim before Qt handles its
own flags; no shell concatenation occurs. `--execute=...` is intentionally rejected.
The tab stays open after the program exits and can be restarted. Explicitly requested
local commands are ordinary local processes, not managed SSH profiles; they may run
a shell or the user's ordinary OpenSSH just as typing in a local terminal can.
CLI command execution cannot be embedded in session YAML or shared route definitions.
The installer leaves the user's existing default terminal untouched.
