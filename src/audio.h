/*
 * Copyright (c) 2006-2009 Spotify Ltd
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 *
 * Audio output driver.
 *
 * This file is part of the libspotify examples suite.
 */
#ifndef _JUKEBOX_AUDIO_H_
#define _JUKEBOX_AUDIO_H_

#include <pthread.h>
#include <stdint.h>
#include <sys/queue.h>

#define RING_QUEUE_SIZE (44100*5)

typedef struct audio_fifo
{
	int16_t samples[RING_QUEUE_SIZE];
	int nsamples;
	uint32_t start;
	uint32_t end;
	int rate;
	int channels;
	pthread_mutex_t mutex;
	pthread_cond_t cond;
	pthread_mutex_t cond_mutex;
} audio_fifo_t;

extern audio_fifo_t g_audio_fifo;

/* --- Functions --- */
extern void
audio_fifo_init(audio_fifo_t *af);
extern void
audio_fifo_flush(audio_fifo_t *af);

#endif /* _JUKEBOX_AUDIO_H_ */
