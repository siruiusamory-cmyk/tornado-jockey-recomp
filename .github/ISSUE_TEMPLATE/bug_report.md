---
name: Bug report
about: Report a crash, a rendering problem, or a behaviour that differs from the original
title: "[Bug] "
labels: ''
assignees: ''
---

## What you ran

- Game version:
- SHA-256 of the `Tornado.exe` (PowerShell:
  `(Get-FileHash -Algorithm SHA256 .\Tornado.exe).Hash`):
- Your `d3d8.conf` contents:
- `faithful=0` or `faithful=1`:

Never attach a game file, save or asset. The hash identifies the copy; the log identifies the
rest.

## Log

Attach the run's log whole. Set `TJ_D3D8_LOG=<path>`, or run
`pwsh -NoProfile -File tools/proxy-run.ps1 -Name bug1` and attach what it writes.

## Description

What went wrong, and how often.

## Steps to reproduce

1.
2.
3.

## Expected and actual

- Expected:
- Actual:

## Additional context

GPU and driver, Windows version, a screenshot, whether `faithful=1` also fails.
