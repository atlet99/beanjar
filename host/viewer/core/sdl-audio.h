// SPDX-FileCopyrightText: Mariotaku <git@mariotaku.me> and the Beanjar contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef VIEWER_SDL_AUDIO_H
#define VIEWER_SDL_AUDIO_H
#include <SDL.h>
/* SDL 2's audio subsystem calls, serialised. The display's audio (audio.c, main thread) and the
 * media mix (media.c, a stream's reader thread) each open their own device, and SDL does not
 * guard SDL_InitSubSystem/SDL_OpenAudioDevice against a second thread: two concurrent inits
 * load the backend (PulseAudio) twice and the loser calls through a null driver hook. The SDL
 * error stays thread-local, so SDL_GetError() after a failure here reports this call's. */
int sdl_audio_init(void);  /* SDL_InitSubSystem(SDL_INIT_AUDIO) */
void sdl_audio_quit(void); /* SDL_QuitSubSystem(SDL_INIT_AUDIO) */
SDL_AudioDeviceID sdl_audio_open(const SDL_AudioSpec *want, SDL_AudioSpec *have);
void sdl_audio_close(SDL_AudioDeviceID dev);
#endif
