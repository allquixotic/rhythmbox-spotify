/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Implementatin of DAAP (iTunes Music Sharing) GStreamer source
 *
 *  Copyright (C) 2005 Charles Schmidt <cschmidt2@emich.edu>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  The Rhythmbox authors hereby grant permission for non-GPL compatible
 *  GStreamer plugins to be used and distributed together with GStreamer
 *  and Rhythmbox. This permission is above and beyond the permissions granted
 *  by the GPL license by which Rhythmbox is covered. If you modify this code
 *  you may extend this exception to your version of the code, but you are not
 *  obligated to do so. If you do not wish to do so, delete this exception
 *  statement from your version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA.
 *
 */

#include "config.h"

#include <sys/time.h>
#include <string.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <netdb.h>
#include <unistd.h>
#include <ctype.h>

#include <libsoup/soup.h>

#include <glib/gi18n.h>
#include <gst/gst.h>
#include <gst/gstcaps.h>
#include <gst/base/gstbasesrc.h>
#include <gst/base/gstpushsrc.h>
#include <libspotify/api.h>

#include <rhythmbox/shell/rb-shell.h>
#include <rhythmbox/sources/rb-browser-source.h>
#include <rhythmbox/plugins/rb-plugin-macros.h>
#include <rhythmbox/lib/rb-debug.h>

#include "rb-spotify-source.h"
#include "rb-spotify-src.h"
#include "rb-spotify-plugin.h"
#include "audio.h"

/* needed for portability to some systems, e.g. Solaris */
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

#define RBSPOTIFYSRC_TYPE (rbspotifysrc_get_type())
#define RBSPOTIFYSRC(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),RBSPOTIFYSRC_TYPE,RBSpotifySrc))
#define RBSPOTIFYSRC_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass),RBSPOTIFYSRC_TYPE,RBSpotifySrcClass))
#define IS_RBSPOTIFYSRC(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj),RBSPOTIFYSRC_TYPE))
#define IS_RBSPOTIFYSRC_CLASS(obj) (G_TYPE_CHECK_CLASS_TYPE((klass),RBSPOTIFYSRC_TYPE))

#define RESPONSE_BUFFER_SIZE	(4096)

typedef struct _RBSpotifySrc RBSpotifySrc;
typedef struct _RBSpotifySrcClass RBSpotifySrcClass;

struct _RBSpotifySrc
{
	GstPushSrc parent;
	GstCaps *caps;
	gchar* uri;
	sp_session *sess;
	sp_track* track;
	gboolean tloaded;
	GstClockTime played;
	int curoffset;

	guint64 size;
};

struct _RBSpotifySrcClass
{
	GstPushSrcClass parent_class;
};

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
		GST_PAD_SRC,
		GST_PAD_ALWAYS,
		GST_STATIC_CAPS_ANY);

GST_DEBUG_CATEGORY_STATIC(rbspotifysrc_debug);
#define GST_CAT_DEFAULT rbspotifysrc_debug

static GstElementDetails rbspotifysrc_details =
		GST_ELEMENT_DETAILS ("RBSpotify Source",
				"Source/Stream",
				"Read a Spotify stream",
				"Ivan Kelly <ivan@bleurgh.com>");

static RBSpotifyPlugin *g_spotify_plugin = NULL;

static void
rbspotifysrc_uri_handler_init(gpointer g_iface, gpointer iface_data);

#define TRACE_SRC 1

static void _do_init(GType spotify_src_type)
{
	static const GInterfaceInfo urihandler_info =
	{ rbspotifysrc_uri_handler_init, NULL, NULL };
	GST_DEBUG_CATEGORY_INIT(rbspotifysrc_debug, "spotifysrc",
			GST_DEBUG_FG_WHITE, "Rhythmbox built in SPOTIFY source element");

	g_type_add_interface_static(spotify_src_type, GST_TYPE_URI_HANDLER,
			&urihandler_info);
}

GST_BOILERPLATE_FULL(RBSpotifySrc, rbspotifysrc, GstElement, GST_TYPE_PUSH_SRC,
		_do_init);

static void
rbspotifysrc_finalize(GObject *object);
static void
rbspotifysrc_set_property(GObject *object, guint prop_id, const GValue *value,
		GParamSpec *pspec);
static void
rbspotifysrc_get_property(GObject *object, guint prop_id, GValue *value,
		GParamSpec *pspec);

static gboolean
rbspotifysrc_start(GstBaseSrc *bsrc);
static gboolean
rbspotifysrc_stop(GstBaseSrc *bsrc);
static gboolean
rbspotifysrc_is_seekable(GstBaseSrc *bsrc);
static gboolean
rbspotifysrc_get_size(GstBaseSrc *src, guint64 *size);
static gboolean
rbspotifysrc_do_seek(GstBaseSrc *src, GstSegment *segment);
static GstFlowReturn
rbspotifysrc_create(GstPushSrc *psrc, GstBuffer **outbuf);

extern pthread_mutex_t g_notify_mutex;
// Synchronization condition variable for the main thread
extern pthread_cond_t g_notify_cond;

int
spcb_music_delivery(sp_session *sess, const sp_audioformat *format,
		const void *frames, int num_frames);

void rbspotifysrc_set_plugin(PeasExtensionBase *plugin)
{
	g_assert(IS_RBSPOTIFYPLUGIN (plugin));
	g_spotify_plugin = RBSPOTIFYPLUGIN (plugin);
}

enum
{
#if 0
	PROP_0,
#endif
	PROP_URI = 1, PROP_SESSION = 2, PROP_SEEKABLE = 3, PROP_BYTESPERREAD = 4
};

static void rbspotifysrc_base_init(gpointer g_class)
{
	GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
	gst_element_class_add_pad_template(element_class,
			gst_static_pad_template_get(&srctemplate));
	gst_element_class_set_details(element_class, &rbspotifysrc_details);
}

static void rbspotifysrc_class_init(RBSpotifySrcClass *klass)
{
	GObjectClass *gobject_class;
	GstElementClass *gstelement_class;
	GstBaseSrcClass *gstbasesrc_class;
	GstPushSrcClass *gstpushsrc_class;

	gobject_class = G_OBJECT_CLASS (klass);
	gstelement_class = GST_ELEMENT_CLASS (klass);
	gstbasesrc_class = (GstBaseSrcClass *) klass;
	gstpushsrc_class = (GstPushSrcClass *) klass;

	parent_class = g_type_class_ref(GST_TYPE_PUSH_SRC);

	gobject_class->set_property = rbspotifysrc_set_property;
	gobject_class->get_property = rbspotifysrc_get_property;
	gobject_class->finalize = rbspotifysrc_finalize;

	g_object_class_install_property(gobject_class, PROP_URI,
			g_param_spec_string("uri", "file uri", "uri of the file to read",
					NULL, G_PARAM_READWRITE));

	g_object_class_install_property(gobject_class, PROP_SESSION,
			g_param_spec_pointer("session", "Spotify session",
					"Spotify session to use", G_PARAM_READWRITE));

	gstbasesrc_class->start = GST_DEBUG_FUNCPTR (rbspotifysrc_start);
	gstbasesrc_class->stop = GST_DEBUG_FUNCPTR (rbspotifysrc_stop);
	gstbasesrc_class->is_seekable =
			GST_DEBUG_FUNCPTR (rbspotifysrc_is_seekable);
	gstbasesrc_class->get_size = GST_DEBUG_FUNCPTR (rbspotifysrc_get_size);
	gstbasesrc_class->do_seek = GST_DEBUG_FUNCPTR (rbspotifysrc_do_seek);

	gstpushsrc_class->create = GST_DEBUG_FUNCPTR (rbspotifysrc_create);
}

static void rbspotifysrc_init(RBSpotifySrc *src, RBSpotifySrcClass *klass)
{
	src->caps = gst_caps_new_simple("audio/x-raw-int", "rate", G_TYPE_INT,
			44100, "channels", G_TYPE_INT, 2, "width", G_TYPE_INT, 16, "depth",
			G_TYPE_INT, 16, "endianness", G_TYPE_INT, G_LITTLE_ENDIAN, "signed",
			G_TYPE_BOOLEAN, TRUE, NULL );
	src->uri = NULL;

	g_object_set(G_OBJECT (src), "session", get_priv()->sess, NULL );
}

static void rbspotifysrc_finalize(GObject *object)
{
	RBSpotifySrc *src;
	src = RBSPOTIFYSRC (object);

	G_OBJECT_CLASS (parent_class) ->finalize(object);
}

static void rbspotifysrc_set_property(GObject *object, guint prop_id,
		const GValue *value, GParamSpec *pspec)
{
	RBSpotifySrc *src = RBSPOTIFYSRC (object);

	switch (prop_id)
	{
	case PROP_URI:
		if (src->uri)
		{
			g_free(src->uri);
			src->uri = NULL;
		}
		src->uri = g_strdup(g_value_get_string(value));
		break;
	case PROP_SESSION:
		src->sess = g_value_get_pointer(value);
		break;

	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		break;
	}
}

static void rbspotifysrc_get_property(GObject *object, guint prop_id,
		GValue *value, GParamSpec *pspec)
{
	RBSpotifySrc *src = RBSPOTIFYSRC (object);

	switch (prop_id)
	{
	case PROP_URI:
		g_value_set_string(value, src->uri);
		break;
	case PROP_SESSION:
		g_value_set_pointer(value, src->sess);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		break;
	}
}

static gboolean rbspotifysrc_start(GstBaseSrc *bsrc)
{
	RBSpotifySrc *src = RBSPOTIFYSRC (bsrc);
//	sp_error err;
	sp_link* link;
	audio_fifo_t *af = &g_audio_fifo;
//	sp_track* track;
//	int timeout;

	g_debug( "Playing %s\n", src->uri);

	link = sp_link_create_from_string(src->uri);
	if (!link)
	{
		g_debug( "Couldn't open file");
		return FALSE;
	}
	src->track = sp_link_as_track(link);
	if (!src->track)
	{
		g_debug( "Couldn't turn link to track %s", src->uri);
		return FALSE;
	}

	sp_track_add_ref(src->track);
	sp_link_release(link);

	src->tloaded = FALSE;
	src->played = 0;

	src->curoffset = 0;

	audio_fifo_flush(af);

	return TRUE;
}

static gboolean rbspotifysrc_stop(GstBaseSrc *bsrc)
{
	/* don't do anything - this seems to get called during setup, but
	 * we don't get started again afterwards.
	 */
	return TRUE;
}

static GstFlowReturn rbspotifysrc_create(GstPushSrc *psrc, GstBuffer **outbuf)
{
	RBSpotifySrc *src;
	size_t readsize = 1000;
	GstBuffer *buf = NULL;
	int err, offset = 0;

	src = RBSPOTIFYSRC (psrc);
	g_debug( "create\n");
	while (!sp_track_is_loaded(src->track))
		sleep(1);
	g_debug( "loaded");

	if (sp_track_is_loaded(src->track) && !src->tloaded)
	{
		readsize = 0;
		g_debug( "Track name %s", sp_track_name(src->track));
		err = sp_session_player_load(src->sess, src->track);
		if (err != SP_ERROR_OK)
		{
			g_debug( "Couldn't load file %x", err);
			return FALSE;
		}

		sp_session_player_play(src->sess, TRUE);
		src->tloaded = TRUE;
	}

	audio_fifo_t *af = &g_audio_fifo;
	struct timeval now;
	struct timespec timeout;

//	if (af->nsamples == 0 && src->curoffset < src->size)
//	{
	g_debug( "samples: %d  curoffset: %lld  size: %lld\n", af->nsamples,
			(unsigned long long) src->curoffset,
			(unsigned long long) src->size);
	pthread_mutex_lock(&af->cond_mutex);
	gettimeofday(&now, NULL );
	timeout.tv_sec = now.tv_sec + 5;
	timeout.tv_nsec = now.tv_usec * 1000;

	pthread_cond_timedwait(&af->cond, &af->cond_mutex, &timeout);
	pthread_mutex_unlock(&af->cond_mutex);

	if (af->nsamples == 0)
	{
		return GST_FLOW_UNEXPECTED;
	}
//	}

	src->caps = gst_caps_new_simple("audio/x-raw-int", "rate", G_TYPE_INT,
			af->rate, "channels", G_TYPE_INT, af->channels, "width", G_TYPE_INT,
			16, "depth", G_TYPE_INT, 16, "endianness", G_TYPE_INT,
			G_LITTLE_ENDIAN, "signed", G_TYPE_BOOLEAN, TRUE, NULL );
	pthread_mutex_lock(&af->mutex);
#ifdef TRACE_SRC
	g_debug( "start: %d end: %d ", af->start, af->end);
	g_debug( "rate: %d channels: %d samples: %d ts: %lld ", af->rate,
			af->channels, af->nsamples, (unsigned long long) src->played);
	g_debug( "seconds: %06f ",
			((double) af->nsamples) / ((double) af->rate));
#endif

	buf = gst_buffer_new_and_alloc(
			af->nsamples * af->channels * sizeof(int16_t));
//	GST_BUFFER_TIMESTAMP (buf) = GST_CLOCK_TIME_NONE;
	GST_BUFFER_TIMESTAMP (buf) = GST_CLOCK_TIME_NONE;
	GST_BUFFER_OFFSET (buf) = src->curoffset;

	src->played += gst_util_uint64_scale_int(GST_SECOND, af->nsamples,
			af->rate);
	GST_BUFFER_SIZE (buf) = af->nsamples * af->channels * sizeof(int16_t);
	src->curoffset = src->curoffset
			+ (af->nsamples * af->channels * sizeof(int16_t));
//	GST_BUFFER_DURATION(buf) = gst_util_uint64_scale_int(GST_SECOND, af->nsamples, af->rate);

	while (af->nsamples > 0)
	{
		int ncopy =
				MIN(RING_QUEUE_SIZE - af->start, af->nsamples * af->channels);
		memcpy(buf->data + offset, &af->samples[af->start],
				ncopy * sizeof(int16_t));

		offset += ncopy * sizeof(int16_t);
		af->nsamples -= ncopy / af->channels;
		af->start = (af->start + ncopy) % RING_QUEUE_SIZE;
	}
	gst_buffer_set_caps(buf, src->caps);
	*outbuf = buf;

#ifdef TRACE_SRC
	g_debug( "unconsumed: %d\n", af->nsamples);
#endif

	pthread_cond_signal(&af->cond);
	pthread_mutex_unlock(&af->mutex);

	return GST_FLOW_OK;
}

gboolean rbspotifysrc_is_seekable(GstBaseSrc *bsrc)
{
	return TRUE;
}

gboolean rbspotifysrc_do_seek(GstBaseSrc *bsrc, GstSegment *segment)
{
//	RBSpotifySrc *src = RBSPOTIFYSRC (bsrc);
	if (segment->format == GST_FORMAT_BYTES)
	{
//		src->do_seek = TRUE;
//		src->seek_bytes = segment->start;
		return TRUE;
	}
	else
	{
		return FALSE;
	}
}

gboolean rbspotifysrc_get_size(GstBaseSrc *bsrc, guint64 *size)
{
	RBSpotifySrc *src = RBSPOTIFYSRC (bsrc);

//	audio_fifo_t *af = &g_audio_fifo;
	if (sp_track_duration(src->track) > 0)
	{
		src->size = 2 * 2 * 44.100 * sp_track_duration(src->track) - 44100;
		*size = src->size;
		g_debug( "Get Size %d\n",
				(sp_track_duration(src->track) / 1000));
		return TRUE;
	}
	else
	{
		return FALSE;
	}
}

static gboolean plugin_init(GstPlugin *plugin)
{
	gboolean ret = gst_element_register(plugin, "rbspotifysrc",
			GST_RANK_PRIMARY, RBSPOTIFYSRC_TYPE);
	return ret;
}

GST_PLUGIN_DEFINE_STATIC(GST_VERSION_MAJOR, GST_VERSION_MINOR, "spotify",
		"element to access Spotify music streams", plugin_init, VERSION, "GPL",
		PACKAGE, "");

/*** GSTURIHANDLER INTERFACE *************************************************/

static guint rbspotifysrc_uri_get_type(void)
{
	return GST_URI_SRC;
}

static gchar **
rbspotifysrc_uri_get_protocols(void)
{
	static gchar *protocols[] =
	{ "spotify", NULL };

	return protocols;
}

static const gchar *
rbspotifysrc_uri_get_uri(GstURIHandler *handler)
{
	RBSpotifySrc *src = RBSPOTIFYSRC (handler);

	return src->uri;
}

static gboolean rbspotifysrc_uri_set_uri(GstURIHandler *handler,
		const gchar *uri)
{
	RBSpotifySrc *src = RBSPOTIFYSRC (handler);

	if (GST_STATE (src) == GST_STATE_PLAYING
			|| GST_STATE (src) == GST_STATE_PAUSED)
	{
		return FALSE;
	}

	g_object_set(G_OBJECT (src), "uri", uri, NULL );

	return TRUE;
}

static void rbspotifysrc_uri_handler_init(gpointer g_iface, gpointer iface_data)
{
	GstURIHandlerInterface *iface = (GstURIHandlerInterface *) g_iface;

	iface->get_type = rbspotifysrc_uri_get_type;
	iface->get_protocols = rbspotifysrc_uri_get_protocols;
	iface->get_uri = rbspotifysrc_uri_get_uri;
	iface->set_uri = rbspotifysrc_uri_set_uri;
}

int spcb_music_delivery(sp_session *sess, const sp_audioformat *format,
		const void *frames, int num_frames)
{
	audio_fifo_t *af = &g_audio_fifo;
	int16_t* samples = (int16_t*) frames;
	int copy_size = 0;

	int frames_consumed = 0;

	if (num_frames == 0)
	{
		pthread_mutex_lock(&g_notify_mutex);
		pthread_cond_signal(&g_notify_cond);
		pthread_mutex_unlock(&g_notify_mutex);

		return 0;
	}
	if ((af->nsamples * format->channels) == RING_QUEUE_SIZE)
		return 0;

	af->rate = format->sample_rate;
	af->channels = format->channels;
	pthread_mutex_lock(&af->mutex);
	/* Buffer one second of audio */
	while (num_frames > 0
			&& (af->nsamples * format->channels) < RING_QUEUE_SIZE)
	{
		copy_size = MIN(RING_QUEUE_SIZE - af->end, num_frames*format->channels);

		memcpy(&af->samples[af->end], samples, copy_size * sizeof(int16_t));

		af->end = (af->end + copy_size) % RING_QUEUE_SIZE;
		af->nsamples += copy_size / format->channels;

		samples += copy_size;
		num_frames -= copy_size / format->channels;
		frames_consumed += copy_size / format->channels;
	}

	g_debug( "start: %d end: %d frames: %d samples: %d\n", af->start,
			af->end, frames_consumed, frames_consumed * 2);
	if ((af->nsamples * format->channels) > (RING_QUEUE_SIZE / 2))
	{
		pthread_mutex_lock(&af->cond_mutex);
		pthread_cond_signal(&af->cond);
		pthread_mutex_unlock(&af->cond_mutex);
	}

	pthread_mutex_unlock(&af->mutex);

	return frames_consumed;
}
