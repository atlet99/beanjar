// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#include "sdl-audio.h"
#include <pthread.h>

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

int sdl_audio_init(void)
{
    pthread_mutex_lock(&lock);
    int r = SDL_InitSubSystem(SDL_INIT_AUDIO);
    pthread_mutex_unlock(&lock);
    return r;
}
void sdl_audio_quit(void)
{
    pthread_mutex_lock(&lock);
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    pthread_mutex_unlock(&lock);
}
SDL_AudioDeviceID sdl_audio_open(const SDL_AudioSpec *want, SDL_AudioSpec *have)
{
    pthread_mutex_lock(&lock);
    SDL_AudioDeviceID dev = SDL_OpenAudioDevice(NULL, 0, want, have, 0);
    pthread_mutex_unlock(&lock);
    return dev;
}
void sdl_audio_close(SDL_AudioDeviceID dev)
{
    pthread_mutex_lock(&lock);
    SDL_CloseAudioDevice(dev);
    pthread_mutex_unlock(&lock);
}
