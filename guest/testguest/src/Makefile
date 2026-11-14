# SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# The test guest's programs; Buildroot passes CC, CFLAGS and LDFLAGS (beanjar-testguest.mk).
PROGRAMS = tg-gpu tg-ports tg-input
# the wire formats shared with the host (the repository's include/; beanjar-testguest.mk passes it)
BEANJAR_INCLUDE ?= ../../../include
CFLAGS += -Wall -Wextra -O2
PREFIX = /usr

all: $(PROGRAMS)

tg-gpu: tg-gpu.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS) -lEGL -lGLESv2 -lgbm

tg-ports: tg-ports.c
	$(CC) $(CFLAGS) -I$(BEANJAR_INCLUDE) -o $@ $< $(LDFLAGS)

tg-input: tg-input.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

install: all
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 0755 $(PROGRAMS) $(DESTDIR)$(PREFIX)/bin/

clean:
	rm -f $(PROGRAMS)
