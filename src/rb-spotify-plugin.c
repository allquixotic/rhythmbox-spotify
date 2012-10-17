/*
 * rb-spotify-plugin.h
 * 
 * Copyright (C) 2002-2005 - Paolo Maggi
 * Copyright (C) 2008-2009 - Ivan Kelly
 * Copyright (C) 2011      - Alexandre Mazari <alexandre.mazari@gmail.com>
 * Copyright (C) 2012      - Sean McNamara <smcnam@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * The Rhythmbox authors hereby grant permission for non-GPL compatible
 * GStreamer plugins to be used and distributed together with GStreamer
 * and Rhythmbox. This permission is above and beyond the permissions granted
 * by the GPL license by which Rhythmbox is covered. If you modify this code
 * you may extend this exception to your version of the code, but you are not
 * obligated to do so. If you do not wish to do so, delete this exception
 * statement from your version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h> /* For strlen */
#include <pthread.h>
#include <gmodule.h>
#include <gtk/gtk.h>
#include <glib.h>
#include <glib-object.h>

#include <gconf/gconf-client.h>
#include <gconf/gconf-value.h>
#include <time.h>

#include <rhythmbox/shell/rb-shell.h>
#include <rhythmbox/lib/rb-debug.h>
#include <rhythmbox/plugins/rb-plugin-macros.h>
#include <rhythmbox/widgets/rb-dialog.h>
#include <rhythmbox/lib/rb-builder-helpers.h>
#include <rhythmbox/lib/rb-file-helpers.h>
#if 0
#include <rhythmbox/lib/rb-preferences.h>
#endif
#include <rhythmbox/sources/rb-display-page-group.h>

#include <libpeas-gtk/peas-gtk.h>
#include <libpeas/peas.h>

#include "rb-spotify-plugin.h"
#include "rb-spotify-source.h"
#include "rb-spotify-src.h"
#include "audio.h"

#define CONF_SPOTIFY_USERNAME CONF_PREFIX "/spotify/username"
#define CONF_SPOTIFY_PASSWORD CONF_PREFIX "/spotify/password"
#define CONF_PREFIX "/apps/rhythmbox"
#define CONF_STATE_SORTING_PREFIX CONF_PREFIX "/state/sorting/"
#define RB_TYPE_SPOTIFY_PLUGIN	(rb_spotify_plugin_get_type ())
#define RB_SPOTIFY_PLUGIN(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_SPOTIFY_PLUGIN, RBSpotifyPlugin))
#define RB_SPOTIFY_PLUGIN_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_SPOTIFY_PLUGIN, RBSpotifyPluginClass))
#define RB_IS_SPOTIFY_PLUGIN(o)	(G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_SPOTIFY_PLUGIN))
#define RB_IS_SPOTIFY_PLUGIN_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_SPOTIFY_PLUGIN))
#define RB_SPOTIFY_PLUGIN_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_SPOTIFY_PLUGIN, RBSpotifyPluginClass))

extern const char g_appkey[];

/// The size of the application key.
extern const size_t g_appkey_size;

/// Synchronization mutex for the main thread
pthread_mutex_t g_notify_mutex;
// Synchronization condition variable for the main thread
pthread_cond_t g_notify_cond;
//static int g_notify_do;
static RBSpotifyPluginPrivate *_priv = NULL;

audio_fifo_t g_audio_fifo;

/**
 * The session callbacks
 */
static gboolean cb_notify_on_main(gpointer userdata);
static void
peas_gtk_configurable_iface_init(PeasGtkConfigurableInterface *iface);
static void
spcb_logged_in(sp_session *sess, sp_error error);
static void
spcb_logged_out(sp_session *sess);
static void
spcb_notify_main_thread(sp_session *sess);
extern int
spcb_music_delivery(sp_session *sess, const sp_audioformat *format,
		const void *frames, int num_frames);
static void
spcb_metadata_updated(sp_session *sess);
static void
spcb_play_token_lost(sp_session *sess);
static void
spcb_connection_error(sp_session *session, sp_error error);
static void
spcb_message_to_user(sp_session *session, const char *message);

#if 0
void* notification_routine (void *s);
#endif

void
rb_spotify_username_entry_focus_out_event_cb(GtkWidget *widget,
		RBSpotifyPlugin *spotify);
void
rb_spotify_username_entry_activate_cb(GtkEntry *entry, RBSpotifyPlugin *spotify);
void
rb_spotify_password_entry_focus_out_event_cb(GtkWidget *widget,
		RBSpotifyPlugin *spotify);

#if 0
void rb_spotify_password_entry_activate_cb (GtkEntry *entry,
		RBSpotifyPlugin *spotify);
#endif

void printthreadname()
{
	gchar *buf = g_new0 (gchar, 255);
	pthread_getname_np(pthread_self(), buf, 255);
	g_debug("Current Thread ID: %u Name: %s\n", pthread_self(), buf);
}

/**
 * This callback is called for log messages.
 *
 * @sa sp_session_callbacks#log_message
 */
static void log_message(sp_session *session, const char *data)
{
	g_debug( "log_message: %s\n", data);
}

static sp_session_callbacks session_callbacks =
{ .logged_in = &spcb_logged_in, .logged_out = &spcb_logged_out,
		.notify_main_thread = &spcb_notify_main_thread, .music_delivery =
				&spcb_music_delivery,
		.metadata_updated = &spcb_metadata_updated, .play_token_lost =
				&spcb_play_token_lost, .log_message = &log_message,
		.connection_error = &spcb_connection_error, .message_to_user =
				&spcb_message_to_user,

};

/**
 * The session configuration. Note that application_key_size is an external, so
 * we set it in plugin init instead.
 */
static sp_session_config spconfig =
{ .api_version = SPOTIFY_API_VERSION, .cache_location = NULL,
		.settings_location = NULL, .application_key = g_appkey,
		.application_key_size = 0, .user_agent = "rhythmbox-spotify",
		.callbacks = &session_callbacks, .userdata = NULL, .compress_playlists =
				FALSE, .dont_save_metadata_for_playlists = FALSE,
		.initially_unload_playlists = FALSE, .proxy = NULL, .proxy_password =
				NULL, .proxy_username = NULL, .tracefile = NULL, .device_id =
				NULL };

G_MODULE_EXPORT void
peas_register_types(PeasObjectModule *module);

static void
rb_spotify_plugin_init(RBSpotifyPlugin *plugin);
#if 0
static void
rb_spotify_plugin_finalize (GObject *object);
#endif
static GtkWidget*
impl_create_configure_widget(PeasGtkConfigurable *plugin);

#define RB_SPOTIFY_PLUGIN_GET_PRIVATE(object) _priv

RB_DEFINE_PLUGIN(RB_TYPE_SPOTIFY_PLUGIN, RBSpotifyPlugin, rb_spotify_plugin,
		(G_IMPLEMENT_INTERFACE_DYNAMIC (PEAS_GTK_TYPE_CONFIGURABLE, peas_gtk_configurable_iface_init)))

G_MODULE_EXPORT void peas_register_types(PeasObjectModule *module)
{
#if 0
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = rb_spotify_plugin_finalize;
#endif
//  RBSpotifyPluginClass *klass = RB_SPOTIFY_PLUGIN_GET_CLASS(module);
	rb_spotify_plugin_register_type(G_TYPE_MODULE (module) );
	peas_object_module_register_extension_type(module, PEAS_TYPE_ACTIVATABLE,
			RB_TYPE_SPOTIFY_PLUGIN);
	peas_object_module_register_extension_type(module,
			PEAS_GTK_TYPE_CONFIGURABLE, RB_TYPE_SPOTIFY_PLUGIN);
	//g_type_class_add_private (klass, sizeof(RBSpotifyPluginPrivate));
}

RBSpotifyPluginPrivate *
get_priv()
{
	if (_priv == NULL )
	{
		_priv = g_new0 (RBSpotifyPluginPrivate, 1);
		g_debug("Initializing priv %p\n", _priv);
		_priv->gconf = gconf_client_get_default();
		spconfig.cache_location = g_get_tmp_dir();
		spconfig.settings_location = g_get_tmp_dir();
		spconfig.tracefile = "/tmp/spotify-trace.log";
	}
	return _priv;
}

static void rb_spotify_plugin_init(RBSpotifyPlugin *plugin)
{
	//plugin->_priv = RB_SPOTIFY_PLUGIN_GET_PRIVATE (plugin);
	if (_priv == NULL )
	{
		_priv = g_new0 (RBSpotifyPluginPrivate, 1);
		g_debug("Initializing priv %p\n", _priv);
		_priv->gconf = gconf_client_get_default();
		spconfig.cache_location = g_get_tmp_dir();
		spconfig.settings_location = g_get_tmp_dir();
		spconfig.tracefile = "/tmp/spotify-trace.log";
	}

	g_debug(
			"+*+*+*+*+*\nRBSpotifyPlugin initialising: %d", plugin);
}

#if 0
static void
rb_spotify_plugin_finalize (GObject *object)
{
	rb_debug("RBSpotifyPlugin finalising");

	G_OBJECT_CLASS (rb_spotify_plugin_parent_class)->finalize (object);
}
#endif

static void peas_gtk_configurable_iface_init(
		PeasGtkConfigurableInterface *iface)
{
	iface->create_configure_widget = impl_create_configure_widget;
}

static void sp_connect(RBSpotifyPluginPrivate *priv)
{
	sp_session *session = priv->sess;
	GConfClient *gconf = priv->gconf;
	char *username = gconf_client_get_string(gconf, CONF_SPOTIFY_USERNAME,
			NULL );
	char *password = gconf_client_get_string(gconf, CONF_SPOTIFY_PASSWORD,
			NULL );
	if (username == NULL || password == NULL )
	{
		rb_error_dialog(NULL, "Spotify Plugin",
				"Username and password not set.");
		return;
	}

	g_debug("sp_session pointer: %p\n", (gpointer) session);
	if (sp_session_connectionstate(session) == SP_CONNECTION_STATE_LOGGED_IN)
		sp_session_logout(session);

	sp_session_login(session, username, password, FALSE, NULL );
}

static void impl_activate(PeasActivatable *plugin)
{
	//	rb_error_dialog (NULL, _("Spotify Plugin"), "Spotify plugin activated, with shell %p", shell);

	RBSpotifySource *source;
	RhythmDBEntryType *type;
	RhythmDB *db;
	char *entry_type_name, *username, *password;
	int err;
	RBSpotifyPlugin *realplugin = (RBSpotifyPlugin*) plugin;
	RBSpotifyPluginPrivate *pprivate = _priv;
	RBShell *shell;

	pthread_mutex_init(&g_notify_mutex, NULL );
	pthread_cond_init(&g_notify_cond, NULL );

	audio_fifo_init(&g_audio_fifo);

	spconfig.application_key_size = g_appkey_size;
	err = sp_session_create(&spconfig, &pprivate->sess);

	if (err != SP_ERROR_OK)
	{
		rb_error_dialog(NULL, "Spotify Plugin",
				"Error initialising spotify session");
		pprivate->sess = NULL;
		return;
	}

	sp_connect(pprivate);

	rbspotifysrc_set_plugin((PeasExtensionBase*) plugin);

	g_object_get(realplugin, "object", &shell, NULL );
	g_object_get(shell, "db", &db, NULL );

	type = g_object_new(RHYTHMDB_TYPE_ENTRY_TYPE, "db", db, "name", "spotify",
			"save-to-disk", FALSE, "category", RHYTHMDB_ENTRY_NORMAL, NULL );
	rhythmdb_register_entry_type(db, type);

	g_object_unref(db);

	source = (RBSpotifySource*) RB_SOURCE (g_object_new (RBSPOTIFYSOURCE_TYPE,
					"name", "spotify",
					"entry-type", type,
					"shell", shell,
					"visibility", TRUE,
					"plugin", realplugin,
					NULL));

	source->priv->sess = pprivate->sess;
	source->priv->db = db;
	source->priv->type = type;

	rb_shell_register_entry_type_for_source(shell, (RBSource*) source, type);

	RBDisplayPageGroup *group = rb_display_page_group_get_by_id("stores");
	//XXX: Potential problem here; compiler warnings
	rb_shell_append_display_page(shell, (RBDisplayPage*) source,
			(RBDisplayPage*) group);
	//	return source;
//  if(shell != NULL)
//    g_object_unref(shell);
}

static void impl_deactivate(PeasActivatable *plugin)
{
	sp_session *session = _priv->sess;
	sp_session_logout(session);
	sp_session_release(session);
}

static void preferences_response_cb(GtkWidget *dialog, gint response,
		PeasExtensionBase *plugin)
{
	gtk_widget_hide(dialog);
	sp_connect(_priv);
}

void rb_spotify_username_entry_focus_out_event_cb(GtkWidget *widget,
		RBSpotifyPlugin *spotify)
{
	GConfClient *gconf = _priv->gconf;
	gconf_client_set_string(gconf, CONF_SPOTIFY_USERNAME,
			gtk_entry_get_text(GTK_ENTRY (widget) ), NULL );
}

void rb_spotify_username_entry_activate_cb(GtkEntry *entry,
		RBSpotifyPlugin *spotify)
{
	RBSpotifyPluginPrivate *pprivate = RB_SPOTIFY_PLUGIN_GET_PRIVATE(spotify);
	gtk_widget_grab_focus(pprivate->password_entry);
}

void rb_spotify_password_entry_focus_out_event_cb(GtkWidget *widget,
		RBSpotifyPlugin *spotify)
{
	GConfClient *gconf = _priv->gconf;
	gconf_client_set_string(gconf, CONF_SPOTIFY_PASSWORD,
			gtk_entry_get_text(GTK_ENTRY (widget) ), NULL );
}

/**
 knicked from audio scrobbler
 */
static GtkWidget*
impl_create_configure_widget(PeasGtkConfigurable *plugin)
{
	RBSpotifyPluginPrivate *pprivate = _priv;
	GConfClient *gconf = pprivate->gconf;

	char* t;
	if (pprivate->config_widget == NULL )
	{
		GtkBuilder *xml;
		char *gladefile;
		//XXX: Compiler warnings
		gladefile = rb_find_plugin_data_file((GObject*) plugin,
				"spotify-prefs.ui");
		g_assert(gladefile != NULL);

		xml = rb_builder_load(gladefile, plugin);

		pprivate->config_widget =
				GTK_WIDGET (gtk_builder_get_object (xml, "spotify_vbox"));
		pprivate->username_entry =
				GTK_WIDGET (gtk_builder_get_object (xml, "username_entry"));
		pprivate->username_label =
				GTK_WIDGET (gtk_builder_get_object (xml, "username_label"));
		pprivate->password_entry =
				GTK_WIDGET (gtk_builder_get_object (xml, "password_entry"));
		pprivate->password_label =
				GTK_WIDGET (gtk_builder_get_object (xml, "password_label"));

		// g_object_unref (G_OBJECT (xml));

		//XXX: Compiler warnings
		t = gconf_client_get_string(gconf, CONF_SPOTIFY_USERNAME, NULL );
		gtk_entry_set_text(GTK_ENTRY (pprivate->username_entry), t ? t : "");
		t = gconf_client_get_string(gconf, CONF_SPOTIFY_PASSWORD, NULL );
		gtk_entry_set_text(GTK_ENTRY (pprivate->password_entry), t ? t : "");
	}

	//gtk_widget_show_all (pprivate->preferences);
	return pprivate->config_widget;
}

void spcb_logged_in(sp_session *sess, sp_error error)
{
	g_debug( "Spotify logged in:%s\n", sp_error_message(error));
}

void spcb_logged_out(sp_session *sess)
{
	g_debug( "Spotify logged out\n");
}

static gboolean cb_notify_on_main(gpointer userdata)
{
	g_debug("cb_notify_on_main()");
	printthreadname();
	sp_session *sess = (sp_session*) userdata;
	int next_timeout;
	g_debug("sp_session pointer in cb_notify_on_main(): %p\n", (gpointer) sess);

	sp_session_process_events(sess, &next_timeout);
	return FALSE;
}


void spcb_notify_main_thread(sp_session *sess)
{
	g_debug( "spcb_notify_main_thread()\n");
	printthreadname();

	g_idle_add((GSourceFunc)cb_notify_on_main, (gpointer)sess);
}

void spcb_metadata_updated(sp_session *sess)
{
	g_debug( "Spotify metadata updated\n");
}

void spcb_play_token_lost(sp_session *sess)
{
	g_debug( "Spotify play token lost\n");
}

void spcb_connection_error(sp_session *session, sp_error error)
{
	g_debug( "Spotify connection error %x\n", sp_error_message(error));
}

void spcb_message_to_user(sp_session *session, const char *message)
{
	g_debug( "Spotify message to user %s\n", message);
}
