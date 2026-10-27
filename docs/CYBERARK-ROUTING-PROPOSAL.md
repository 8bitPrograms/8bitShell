# CyberArk PSMP routing proposal

Status: historical proposal, superseded by the [implemented bounded PSMP routing](CYBERARK.md).
The YAML below is still a design sketch, **not the runtime import format**.
The accompanying UI change only changes panel transitions. No connection policy,
credentials, real catalog, or transport validation is changed by it.

## User-facing model

Separate the destination, the route, and the operator's personal identity:

- Personal settings: one employee username, under a stable identity alias such as
  `work`. Colleagues supply their own value for the same alias. This value is never
  included in shared-session exports. Passwords and MFA answers are not YAML data.
- Shared route: PSMP gateway host/port, identity alias, and supported username
  grammar. An organization defines this once; each session keeps its actual target
  host, not the gateway hostname.
- Shared account rule: derive an account from the personal username plus a suffix
  (for example `SYS`), or use a fixed account such as `opc`. Domain is optional and
  explicit. Do not lowercase employee IDs or account names when normalizing session
  display names.
- Session/folder assignment: choose `inherit`, `direct`, or a named PSMP route.
  A direct session can use a literal username or an explicitly chosen personal
  identity; it never automatically acquires the PSMP username or suffix.

Effective routing is selected from the session, then the closest ancestor folder
with an explicit assignment. `direct` is a real override, not an empty value.
Select the route atomically: do not accidentally combine a gateway from one folder
with an incompatible account rule from another. A session can override the account
rule within its selected route. With no assignment anywhere, a legacy session
remains direct.

## Illustrative YAML (draft only)

These are proposed fields, not additions to the current version-1 import format.
The fictional values below deliberately contain no actual employee or server data.

Personal, local-only settings:

```yaml
identities:
  work:
    username: EMP000123
```

Shared catalog/profile concept:

```yaml
routes:
  company-psmp:
    type: cyberark-psmp
    gateway: {host: psmp.example.test, port: 22}
    identity: work
    usernameFormat: user-account-target
    defaultAccount:
      source: identity
      suffix: SYS
      domain: corp.example.test

folderConnections:
  Databases: {mode: route, route: company-psmp}

sessions:
  - id: database-one
    host: db01.example.test
    folder: Databases/Production
    connection: {mode: inherit}

  - id: cloud-database
    host: cloud-db02.example.test
    folder: Databases/Cloud
    connection:
      mode: inherit
      account: {source: literal, name: opc} # no domain, no SYS suffix

  - id: direct-exception
    host: lab01.example.test
    folder: Databases/Lab
    connection: {mode: direct, user: oracle}
```

For the supplied organization-specific grammar, the first two resolve to:

| Destination | Network endpoint | Login username |
| --- | --- | --- |
| db01.example.test | psmp.example.test:22 | `EMP000123@EMP000123SYS%corp.example.test@db01.example.test` |
| cloud-db02.example.test | psmp.example.test:22 | `EMP000123@opc@cloud-db02.example.test` |
| lab01.example.test | lab01.example.test:22 | `oracle` |

The grammar is based on the user's working examples, not a claim that every PSMP
installation uses this exact syntax. Other company formats should be named,
validated formatters or bounded, allowlisted token templates, never scripts or
arbitrary command-line fragments. Missing identities, invalid components, unknown
routes, or unavailable inherited rules block a managed connection.

## Execution and security boundary

The resolver should produce one immutable connection plan containing:
logical target, selected route and provenance, network endpoint, login username,
personal identity reference, protocol capabilities, and the existing SSH policy.
Terminal launch/restart, SFTP, monitoring, and managed Quick Connect must use the
same resolution logic. Running sessions keep their original plan when shared data
or personal settings change; show that a reconnect is needed to adopt changes.

For terminal SSH, pass the composite username through `-l` as one argument, with
the gateway hostname as a separate argument. Do not execute a concatenated shell
string. OpenSSH documents `-l` as the remote login name; `-J` is a different mechanism
that forwards TCP through a jump host, so the supplied PSMP login grammar should
not be implemented by blindly substituting ProxyJump.
[Primary reference: OpenSSH ssh(1)](https://man.openbsd.org/ssh.1).

Validate operator, account, domain, and target components before formatting. Reject
control characters, whitespace, injected delimiters, unsupported fields, and
oversized results. Permit the literal `%` separator in a validated PSMP login
without loosening unrelated key-path token restrictions. Keep host-key checking,
cryptographic policy, and forwarding restrictions unchanged. Never fall back to a
direct target after a missing CyberArk identity, route error, or authentication
failure.

SSH host-key verification covers the actual network endpoint: the PSMP gateway.
This is not an independent client-side verification of the final target's host
key; the gateway's target-side controls remain the organization's responsibility.
Show the target and gateway separately in the connection preview and logs. New or
changed gateways in shared data require explicit trust/review before sending a
personal identity. An import must not silently replace a trusted route with an
unrelated one sharing the same ID. Version the new schema; never ignore unknown
routing fields and then connect directly.

Passwords/MFA remain interactive and are not replayed from shared configuration.
SFTP and monitoring are separate processes today and use batch authentication;
an authenticated terminal does not establish support for either. Keep them gated
by route capabilities and validate them against the organization's PSMP policy.
If unavailable, explain why; do not bypass PSMP or reuse terminal passwords.
Monitoring remains off by default. Any eventual prompt automation requires its
own bounded, opt-in conversation design and sensitive-input handling.

Managed Quick Connect should resolve a saved session before considering a literal
direct target. A plain system `ssh` typed into a local shell is not currently
intercepted. Any future opt-in `bitshellctl` integration must use the same resolver,
preserve explicit unmanaged commands, and never replace the system SSH binary.

## Current gaps and implementation acceptance

As inspected on 2026-09-11:

- `ConnectionResolver` has template and policy-selection tests, but TerminalView,
  SftpController, and HostMonitor call `SshSecurity` directly. The prototype alone
  is not working CyberArk support.
- `SshSecurity::safeUser` rejects `%`, so the domain-bearing example is currently
  rejected. A route-aware validation/command plan is needed, not just removing the
  current validation.
- Version-1 catalogs/settings have no runtime personal identity, route assignment,
  or folder connection inheritance. Team synchronization is also future work.

Before enabling this design: test both username forms byte-for-byte, different
colleagues using the same catalog, nearest-folder inheritance, explicit direct
exceptions, local-only export exclusions, unknown-route failures, route-ID
collisions, delimiter/control-character injection, and host-key changes. Verify
launch/restart and all permitted protocols consume the same resolved plan. Keep
current hardening tests passing and complete a user-authorized live PSMP/MFA test;
fixtures alone cannot establish institutional compatibility.
