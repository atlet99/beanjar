# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# On PYTHONPATH in a coverage build's tests (meson/coverage/meson.build): every Python process
# they start, the tests and the beanjar processes those spawn, records coverage.py data for
# COVERAGE_PROCESS_START's configuration. Interpreters without coverage.py (mitmweb's, a pipx
# venv) run as before.
try:
    import coverage
except ImportError:
    pass
else:
    coverage.process_startup()
