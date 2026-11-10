# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# A coverage build of the host programs (docs/dev/guides/coverage.md): set up with -Db_coverage=true,
# its `meson test` records gcov counters for the C programs and coverage.py data for every Python
# process of the tests, and `ninja coverage-report` (or meson's own `ninja coverage`, C only)
# writes the report into meson-logs/.
#
# coverage_c: linked into media-helper, which has no SIGTERM handler of its own, so that a SIGTERM
# still writes its counters (gcov-flush.c); empty in any other build.
coverage_c = []
if get_option('b_coverage')
  coverage_c = files('gcov-flush.c')
  coveragerc = configure_file(input: 'coveragerc.in', output: 'coveragerc',
    configuration: {'TOP': top, 'DATA': meson.project_build_root() / 'meson-logs' / 'coverage-python'})
  # sitecustomize.py starts coverage.py in every Python process the tests run, and they in turn,
  # unless one is run with -I or -S or a PYTHONPATH of its own.
  coverage_env = environment({'COVERAGE_PROCESS_START': meson.current_build_dir() / 'coveragerc'})
  coverage_env.prepend('PYTHONPATH', meson.current_source_dir())
  add_test_setup('coverage', env: coverage_env, is_default: true)
  run_target('coverage-report',
    command: [python, files('report.py'), top, meson.project_build_root()])
endif
