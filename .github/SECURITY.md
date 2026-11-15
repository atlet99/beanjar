# Security policy

## Reporting a vulnerability

Report a vulnerability privately. On the public repository (webosbrew/beanjar), open the Security tab and choose "Report a vulnerability" (GitHub private vulnerability reporting). Please do not open a public issue or pull request for it.

Say what is affected, how to reproduce it and what an attacker gains. A short description is enough to start.

Never include firmware, keys, model secrets or VM identity files in a report. Describe them instead, and we will ask for what we need.

## Scope

In scope is Beanjar's own code: the host programs and services (manager, viewer, web interface, VNC, helpers), the guest shims, layers and kernel modules, and the packaging.

Out of scope is LG's firmware itself, which runs unmodified in the emulator. Report problems in it to LG.

## Supported versions

There are no releases yet, so only `main` is supported. After the first release, the latest release and `main` are.
