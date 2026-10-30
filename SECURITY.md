# Security policy

8bitShell is currently a pre-release Linux application with a native Windows preview. The hardening controls and local
validation evidence are recorded in the maintainers' security assessment, kept outside this repository.
There is no independent security certification, FIPS claim, or supported enterprise LTS
release at this stage. Windows and future protocol/credential integrations are not covered
by the Linux assessment. The native Windows implementation and remaining acceptance
work are described separately in [Windows support](docs/WINDOWS.md).

## Deployment requirements

- Use security-supported OS, OpenSSH, Qt, libvterm, and YAML packages. A version string alone
  does not establish patch status; distribution backports need vendor advisory evidence.
  Current builds bundle a pinned libvterm source with the defensive parser patch in
  `cmake/LibvtermPatch.cmake`; the OS libvterm package is no longer the runtime parser.
  Official packages also bundle the PuTTYgen 0.85 command-line converter built from its
  SHA-256-locked official source for one-time PPK import. It is not an SSH transport;
  system OpenSSH remains mandatory.
- Run as an ordinary desktop user, never root or set-id. Provision independently verified
  server keys or SSH host CA trust before connecting.
- Treat catalogs, macros, downloads, terminal output, logs, and screenshots as sensitive.
  Treat managed private-key copies as sensitive too. Do not store passwords/tokens in YAML
  or macros. Local terminal access is intentionally
  unrestricted; the app's SSH policy is not an endpoint-wide enforcement mechanism.
- Do not enable legacy crypto, unknown-host acceptance, agent forwarding, or arbitrary SSH
  options to “fix” a failed connection. Resolve the trust/server compatibility issue.
- Run the release and security test presets and retain dependency/binary evidence for each
  build. These checks supplement, not replace, independent assessment and vulnerability triage.

## Reporting a suspected vulnerability

A dedicated private disclosure address and response SLA have **not yet been established**.
Before external distribution, the project owner must publish a monitored private channel,
ownership, an escalation path, and a supported-version policy. Until then, use an existing
private channel to the project owner; do not publish secrets or operational exploit details
in a public issue. This document does not invent an email address or promise a response time.

A useful private report includes the affected build/hash, OS/dependency versions, expected
and actual behavior, prerequisites, and a minimal reproduction using disposable local data.
Redact credentials, private keys, host inventories, and user terminal logs. Test only systems
you own or are explicitly authorized to assess. Coordinate a fix and disclosure date with
the owner before public release.

## Release gates still required

The owner must approve an independent penetration test, real enterprise SSH/SFTP/MFA/CA
compatibility results, dependency advisory triage, full transitive SBOM, signed artifact and
update distribution, and an operational log/credential policy before claiming enterprise
production readiness. A checklist item is not evidence that a service or process is running.
