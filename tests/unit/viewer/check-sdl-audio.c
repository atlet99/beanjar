// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
/* The viewer opens SDL audio from two threads: the display's audio (main) and the media mix (a
 * stream's reader). core/sdl-audio.c serialises SDL's audio init, open, close and quit; here
 * several threads do that cycle at once on SDL's dummy driver, which unguarded crashes or finds
 * the subsystem uninitialised. The dummy driver has one device, so an open refused as already
 * open is expected. Afterwards the subsystem must be balanced back to uninitialised. */
#define _POSIX_C_SOURCE 200809L
#include "../../../host/viewer/core/sdl-audio.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define THREADS 8
#define ROUNDS  100

static void callback(void *data, Uint8 *out, int len)
{
    (void)data;
    SDL_memset(out, 0, len);
}

static void *cycle(void *arg)
{
    long failures = 0;
    for (int n = 0; n < ROUNDS; n++) {
        if (sdl_audio_init() < 0) {
            fprintf(stderr, "init: %s\n", SDL_GetError());
            failures++;
            continue;
        }
        SDL_AudioSpec want = {.freq = 48000,
                              .format = AUDIO_F32SYS,
                              .channels = 2,
                              .samples = 256,
                              .callback = (n & 1) ? callback : NULL},
                      have;
        SDL_AudioDeviceID dev = sdl_audio_open(&want, &have);
        if (dev)
            sdl_audio_close(dev);
        else if (!strstr(SDL_GetError(), "already open")) {
            fprintf(stderr, "open: %s\n", SDL_GetError());
            failures++;
        }
        sdl_audio_quit();
    }
    *(long *)arg = failures;
    return NULL;
}

int main(void)
{
    setenv("SDL_AUDIODRIVER", "dummy", 1);
    pthread_t t[THREADS];
    long failures[THREADS] = {0}, total = 0;
    for (int n = 0; n < THREADS; n++)
        pthread_create(&t[n], NULL, cycle, &failures[n]);
    for (int n = 0; n < THREADS; n++) {
        pthread_join(t[n], NULL);
        total += failures[n];
    }
    if (total) {
        fprintf(stderr, "check-sdl-audio: %ld failed calls\n", total);
        return 1;
    }
    if (SDL_WasInit(SDL_INIT_AUDIO)) {
        fprintf(stderr, "check-sdl-audio: audio left initialised\n");
        return 1;
    }
    printf("check-sdl-audio: %d threads x %d init/open/close/quit cycles\n", THREADS, ROUNDS);
    return 0;
}
