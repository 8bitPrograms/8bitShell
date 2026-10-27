# ADR 0001: Interactive terminal bootstrap

## Status

Accepted as an interim implementation on 2026-09-11.

## Decision

Use the system libvterm 0.3.3 package for the first live terminal milestone, with a Linux PTY
host and a Qt Quick painted surface owned by the application.

This is a reference implementation, not the final terminal-engine selection. The planned
Contour `vtbackend`/`vtrasterizer` and `libghostty-vt` bake-off remains open.

## Why

- It gives the product a real interactive terminal without introducing GTK, libadwaita, a web
  view, or a QWidget island.
- Terminal output, renderer decorations, clipboard text, and plain-text logging remain separate.
- The PTY test provides a concrete behavioral baseline for future engine adapters.

## Current boundary

The implementation supports local shells, system OpenSSH, ANSI color, alternate screens,
terminal mouse reporting, resize, scrollback, selection, clipboard, zoom, and rendering-only
highlight rules.

The following remain deliberate follow-up work: dedicated protocol workers, full grapheme and
emoji validation, hyperlinks, search, multiple tabs and splits, configuration loading, and
Windows ConPTY.
