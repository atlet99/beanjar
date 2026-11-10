# Contributing to Beanjar

Thank you for helping. Bug reports, fixes, new firmware targets and documentation are all welcome.

## Build and test

Beanjar builds on Debian 13. [Building](../docs/dev/guides/build.md) covers the host packages, the
host and guest builds and the Debian packages; [Testing](../docs/dev/guides/testing.md) covers the
tiers and how to write a test. The short version:

```sh
pytest tests/unit                     # unit tests: nothing built, no firmware
scripts/build.sh host
pytest tests/integration              # integration tests: need this checkout's builds
```

The unit tests must pass for every change; run the ones for the area you touched. Integration
tests need the build they exercise and skip what is missing. The regress tier boots firmware in
VMs and needs a VM host (KVM on a Raspberry Pi 5 or a PC), so you are not expected to run it;
say in the pull request what you could not test and a maintainer will run it.

## Formatting

Python is formatted with Ruff and C with clang-format. Install the tools and the pre-commit hooks
once (`python3 scripts/format-hooks/install-hooks.py`); a commit that needs reformatting is blocked until
you stage the formatter's changes. `scripts/format check` checks the whole tree and
`scripts/format all` formats it. The versions and the rules are in
[Formatting](../docs/dev/guides/formatting.md).

## Commits

Make one change per commit, so that each commit builds and its tests pass. Write a short subject
line, in the style of `git log` (a sentence that says what changed, with the area first when it
helps), then a short paragraph on what changed and why. A commit message is not an essay.

## Licence

Beanjar is licensed GPL-2.0-or-later. By submitting a contribution you license it under the same
terms (inbound = outbound). Do not copy in code under an incompatible licence.

## Firmware, keys and secrets

Beanjar does not ship firmware and never will: you supply your own. Never attach firmware,
firmware pieces or files extracted from firmware (including layer captures and decompiled code),
keys, model secrets, or a VM's identity and secure-store files to an issue or a pull request. VM
logs can contain the model secret; remove it before sharing a log.

## Tools that assist

Contributions written with the help of AI tools follow the same rules as any other: you are
responsible for the change, and it must build, pass the tests and be understood by its author.
