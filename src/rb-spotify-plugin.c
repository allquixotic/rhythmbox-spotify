/*
 * rb-sample-plugin.h
 * 
 * Copyright (C) 2002-2005 - Paolo Maggi
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

#include <gmodule.h>
#include <gtk/gtk.h>
#include <glib.h>
#include <glib-object.h>

#include <gconf/gconf-value.h>
#include <time.h>

#include <rhythmbox/shell/rb-shell.h>
#include <rhythmbox/lib/rb-debug.h>
#include <rhythmbox/shell/rb-plugin.h>
#include <rhythmbox/widgets/rb-dialog.h>
#include <rhythmbox/lib/rb-builder-helpers.h>
#include <rhythmbox/lib/rb-preferences.h>
#include <rhythmbox/sources/rb-display-page-group.h>

#include "rb-spotify-plugin.h"
#include "rb-spotify-source.h"
#include "rb-spotify-src.h"
#include "audio.h"

#define CONF_SPOTIFY_USERNAME CONF_PREFIX "/spotify/username"
#define CONF_SPOTIFY_PASSWORD CONF_PREFIX "/spotify/password"

extern const char g_appkey[];

/// The size of the application key.
extern const size_t g_appkey_size;

/// Synchronization mutex for the main thread
pthread_mutex_t g_notify_mutex;
// Synchronization condition variable for the main thread
pthread_cond_t g_notify_cond;
static int g_notify_do;

audio_fifo_t g_audio_fifo;

/**
 * The session callbacks
 */
static void
spcb_logged_in (sp_session *sess, sp_error error);
static void
spcb_logged_out (sp_session *sess);
static void
spcb_notify_main_thread (sp_session *sess);
extern int
spcb_music_delivery (sp_session *sess,
                     const sp_audioformat *format,
                     const void *frames,
                     int num_frames);
static void
spcb_metadata_updated (sp_session *sess);
static void
spcb_play_token_lost (sp_session *sess);
static void
spcb_connection_error (sp_session *session, sp_error error);
static void
spcb_message_to_user (sp_session *session, const char *message);

void*
notification_routine (void *s);

void
rb_spotify_username_entry_focus_out_event_cb (GtkWidget *widget,
                                              RBSpotifyPlugin *spotify);
void
rb_spotify_username_entry_activate_cb (GtkEntry *entry,
                                       RBSpotifyPlugin *spotify);
void
rb_spotify_password_entry_focus_out_event_cb (GtkWidget *widget,
                                              RBSpotifyPlugin *spotify);
void
rb_spotify_password_entry_activate_cb (GtkEntry *entry,
                                       RBSpotifyPlugin *spotify);

/**
 * This callback is called for log messages.
 *
 * @sa sp_session_callbacks#log_message
 */
static void
log_message (sp_session *session, const char *data)
{
  fprintf (stderr, "log_message: %s\n", data);
}

static sp_session_callbacks session_callbacks =
  { .logged_in = &spcb_logged_in, .logged_out = &spcb_logged_out,
      .notify_main_thread = &spcb_notify_main_thread, .music_delivery =
          &spcb_music_delivery, .metadata_updated = &spcb_metadata_updated,
      .play_token_lost = &spcb_play_token_lost, .log_message = &log_message,
      .connection_error = &spcb_connection_error, .message_to_user =
          &spcb_message_to_user,

  };

/**
 * The session configuration. Note that application_key_size is an external, so
 * we set it in main() instead.
 */
static sp_session_config spconfig =
  { .api_version = SPOTIFY_API_VERSION, .cache_location = "tmp",
      .settings_location = "tmp", .application_key = g_appkey,
      .application_key_size = 0, // Set in main()
      .user_agent = "testexamples", .callbacks = &session_callbacks, NULL, };

G_MODULE_EXPORT GType
register_rb_plugin (GTypeModule *module);

static void
rb_spotify_plugin_init (RBSpotifyPlugin *plugin);
static void
rb_spotify_plugin_finalize (GObject *object);
static void
impl_activate (RBPlugin *plugin, RBShell *shell);
static void
impl_deactivate (RBPlugin *plugin, RBShell *shell);
static GtkWidget*
impl_create_configure_dialog (RBPlugin *plugin);
RB_PLUGIN_REGISTER(RBSpotifyPlugin, rb_spotify_plugin)
#define RB_SPOTIFY_PLUGIN_GET_PRIVATE(object) (G_TYPE_INSTANCE_GET_PRIVATE ((object), RBSPOTIFYPLUGIN_TYPE, RBSpotifyPluginPrivate))

static void
rb_spotify_plugin_class_init (RBSpotifyPluginClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  RBPluginClass *plugin_class = RB_PLUGIN_CLASS (klass);

  object_class->finalize = rb_spotify_plugin_finalize;

  plugin_class->activate = impl_activate;
  plugin_class->deactivate = impl_deactivate;
  plugin_class->create_configure_dialog = impl_create_configure_dialog;

  g_type_class_add_private (object_class, sizeof(RBSpotifyPluginPrivate));
}

static void
rb_spotify_plugin_init (RBSpotifyPlugin *plugin)
{
  plugin->priv = RB_SPOTIFY_PLUGIN_GET_PRIVATE (plugin);

  rb_debug ("RBSpotifyPlugin initialising");
}

static void
rb_spotify_plugin_finalize (GObject *object)
{
  /*
   RBSamplePlugin *plugin = RB_SAMPLE_PLUGIN (object);
   */
  rb_debug ("RBSpotifyPlugin finalising");

  G_OBJECT_CLASS (rb_spotify_plugin_parent_class)->finalize (object);
}

static void
sp_connect (sp_session *session)
{
  char *username = eel_gconf_get_string (CONF_SPOTIFY_USERNAME);
  char *password = eel_gconf_get_string (CONF_SPOTIFY_PASSWORD);
  if (username == NULL || password == NULL)
  {
    rb_error_dialog (NULL, "Spotify Plugin", "Username and password not set.");
    return;
  }

  if (sp_session_connectionstate (session) == SP_CONNECTION_STATE_LOGGED_IN)
    sp_session_logout (session);

  sp_session_login (session, username, password);
}

static void
impl_activate (RBPlugin *plugin, RBShell *shell)
{
  //	rb_error_dialog (NULL, _("Spotify Plugin"), "Spotify plugin activated, with shell %p", shell);


  RBSpotifySource *source;
  RhythmDBEntryType *type;
  RhythmDB *db;
  char *entry_type_name, *username, *password;
  int err;
  RBSpotifyPluginPrivate *pprivate = RBSPOTIFYPLUGIN(plugin)->priv;

  pthread_mutex_init (&g_notify_mutex, NULL);
  pthread_cond_init (&g_notify_cond, NULL);

  audio_fifo_init (&g_audio_fifo);

  spconfig.application_key_size = g_appkey_size;
  err = sp_session_create (&spconfig, &pprivate->sess);

  if (err != SP_ERROR_OK)
  {
    rb_error_dialog (NULL,
                     "Spotify Plugin",
                     "Error initialising spotify session");
    pprivate->sess = NULL;
    return;
  }

  sp_connect (pprivate->sess);

  rbspotifysrc_set_plugin (plugin);

  g_object_get (shell, "db", &db, NULL);

  type = g_object_new (RHYTHMDB_TYPE_ENTRY_TYPE,
                       "db",
                       db,
                       "name",
                       "spotify",
                       "save-to-disk",
                       FALSE,
                       "category",
                       RHYTHMDB_ENTRY_NORMAL,
                       NULL);
  rhythmdb_register_entry_type (db, type);

  g_object_unref (db);

  source = (RBSpotifySource*) RB_SOURCE (g_object_new (RBSPOTIFYSOURCE_TYPE,
          "name", "spotify",
          "entry-type", type,
          "shell", shell,
          "visibility", TRUE,
          //					  "sorting-key", CONF_STATE_SORTING,
      "plugin", RB_PLUGIN (plugin),
      NULL));

  source->priv->sess = pprivate->sess;
  source->priv->db = db;
  source->priv->type = type;

  rb_shell_register_entry_type_for_source (shell, (RBSource*) source, type);

  RBDisplayPageGroup *group = rb_display_page_group_get_by_id ("stores");
  rb_shell_append_display_page (shell, source, group);
  //	return source;
}

static void
impl_deactivate (RBPlugin *plugin, RBShell *shell)
{
  sp_session *session = RBSPOTIFYPLUGIN(plugin)->priv->sess;
  sp_session_logout (session);
  sp_session_release (session);
}

static void
preferences_response_cb (GtkWidget *dialog, gint response, RBPlugin *plugin)
{
  gtk_widget_hide (dialog);
  sp_connect (RBSPOTIFYPLUGIN(plugin)->priv->sess);
}

void
rb_spotify_username_entry_focus_out_event_cb (GtkWidget *widget,
                                              RBSpotifyPlugin *spotify)
{
  eel_gconf_set_string (CONF_SPOTIFY_USERNAME,
                        gtk_entry_get_text (GTK_ENTRY (widget)));
}

void
rb_spotify_username_entry_activate_cb (GtkEntry *entry,
                                       RBSpotifyPlugin *spotify)
{
  RBSpotifyPluginPrivate *pprivate = RB_SPOTIFY_PLUGIN_GET_PRIVATE(spotify);
  gtk_widget_grab_focus (pprivate->password_entry);
}

void
rb_spotify_password_entry_focus_out_event_cb (GtkWidget *widget,
                                              RBSpotifyPlugin *spotify)
{
  eel_gconf_set_string (CONF_SPOTIFY_PASSWORD,
                        gtk_entry_get_text (GTK_ENTRY (widget)));
}

/**
 knicked from audio scrobbler
 */
static GtkWidget*
impl_create_configure_dialog (RBPlugin *plugin)
{
  RBSpotifyPluginPrivate *pprivate = RB_SPOTIFY_PLUGIN_GET_PRIVATE(plugin);

  if (pprivate->preferences == NULL)
  {
    char* t;

    if (pprivate->config_widget == NULL)
    {
      GtkBuilder *xml;
      char *gladefile;

      gladefile = rb_plugin_find_file (plugin, "spotify-prefs.ui");
      g_assert (gladefile != NULL);

      xml = rb_builder_load (gladefile, plugin);

      pprivate->config_widget
          = GTK_WIDGET(gtk_builder_get_object(xml, "spotify_vbox"));
      pprivate->username_entry
          = GTK_WIDGET(gtk_builder_get_object (xml, "username_entry"));
      pprivate->username_label
          = GTK_WIDGET(gtk_builder_get_object(xml, "username_label"));
      pprivate->password_entry
          = GTK_WIDGET(gtk_builder_get_object(xml, "password_entry"));
      pprivate->password_label
          = GTK_WIDGET(gtk_builder_get_object(xml, "password_label"));

      // g_object_unref (G_OBJECT (xml));

    }

    t = eel_gconf_get_string (CONF_SPOTIFY_USERNAME);
    gtk_entry_set_text (GTK_ENTRY (pprivate->username_entry), t ? t : "");
    t = eel_gconf_get_string (CONF_SPOTIFY_PASSWORD);
    gtk_entry_set_text (GTK_ENTRY (pprivate->password_entry), t ? t : "");

    pprivate->preferences
        = gtk_dialog_new_with_buttons ("Spotify Preferences",
                                       NULL,
                                       GTK_DIALOG_DESTROY_WITH_PARENT,
                                       GTK_STOCK_CLOSE,
                                       GTK_RESPONSE_CLOSE,
                                       NULL);
    gtk_container_set_border_width (GTK_CONTAINER (pprivate->preferences), 5);
    gtk_window_set_resizable (GTK_WINDOW (pprivate->preferences), FALSE);

    g_signal_connect (G_OBJECT (pprivate->preferences),
        "response",
        G_CALLBACK (preferences_response_cb),
        plugin);
    gtk_widget_hide_on_delete (pprivate->preferences);

    gtk_container_add (GTK_CONTAINER (gtk_dialog_get_content_area(GTK_DIALOG (pprivate->preferences))),
                       pprivate->config_widget);
  }

  gtk_widget_show_all (pprivate->preferences);
  return pprivate->preferences;
}

void
spcb_logged_in (sp_session *sess, sp_error error)
{
  fprintf (stderr, "Spotify logged in:%s\n", sp_error_message (error));
}

void
spcb_logged_out (sp_session *sess)
{
  fprintf (stderr, "Spotify logged out\n");
}

void
spcb_notify_main_thread (sp_session *sess)
{
  fprintf (stderr, "Spotify notify\n");

  int next_timeout;

  sp_session_process_events (sess, &next_timeout);
}

void
spcb_metadata_updated (sp_session *sess)
{
  fprintf (stderr, "Spotify metadata updated\n");
}

void
spcb_play_token_lost (sp_session *sess)
{
  fprintf (stderr, "Spotify play token lost\n");
}

void
spcb_connection_error (sp_session *session, sp_error error)
{
  fprintf (stderr, "Spotify connection error %x\n", sp_error_message (error));
}

void
spcb_message_to_user (sp_session *session, const char *message)
{
  fprintf (stderr, "Spotify message to user %s\n", message);
}
