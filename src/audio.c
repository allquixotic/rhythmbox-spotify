#include "audio.h"

void audio_fifo_init(audio_fifo_t *af)
{
	af->nsamples = 0;
	af->start = 0;
	af->end = 0;

	pthread_mutex_init(&af->mutex, NULL );
	pthread_mutex_init(&af->cond_mutex, NULL );
	pthread_cond_init(&af->cond, NULL );
}

void audio_fifo_flush(audio_fifo_t *af)
{
	pthread_mutex_lock(&af->mutex);
	af->nsamples = 0;
	af->start = 0;
	af->end = 0;
	pthread_mutex_unlock(&af->mutex);
}

