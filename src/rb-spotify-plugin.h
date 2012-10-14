/*
 * Copyright/Licensing information.
 */

/* inclusion guard */
#ifndef __RBSPOTIFYPLUGIN_H__
#define __RBSPOTIFYPLUGIN_H__

#include <glib-object.h>
#include <libspotify/api.h>
#include <pthread.h>

/*
 * Potentially, include other headers on which this header depends.
 */
#include <rhythmbox/shell/rb-shell.h>
#include <rhythmbox/sources/rb-browser-source.h>
#include <rhythmbox/plugins/rb-plugin-macros.h>
#include <gconf/gconf.h>
#include <gconf/gconf-client.h>
#include <libpeas-gtk/peas-gtk.h>


G_BEGIN_DECLS


#define RBSPOTIFYPLUGIN_TYPE		(rb_spotify_plugin_get_type ())
#define RBSPOTIFYPLUGIN(o)			(G_TYPE_CHECK_INSTANCE_CAST ((o), RBSPOTIFYPLUGIN_TYPE, RBSpotifyPlugin))
#define RBSPOTIFYPLUGIN_CLASS(k)		(G_TYPE_CHECK_CLASS_CAST((k), RBSPOTIFYPLUGIN_TYPE, RBSpotifyPluginClass))
#define IS_RBSPOTIFYPLUGIN(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), RBSPOTIFYPLUGIN_TYPE))
#define IS_RBSPOTIFYPLUGIN_CLASS(k)		(G_TYPE_CHECK_CLASS_TYPE ((k), RBSPOTIFYPLUGIN_TYPE))
#define RBSPOTIFYPLUGIN_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), RBSPOTIFYPLUGIN_TYPE, RBSpotifyPluginClass))

typedef struct
{
     sp_session *sess;
     pthread_t notify_thread;

  //libpeas gives us our own toplevel
  #if 0
     GtkWidget *preferences;
  #endif
     GtkWidget *config_widget;

     GtkWidget *username_entry;
     GtkWidget *username_label;
     GtkWidget *password_entry;
     GtkWidget *password_label;
     GConfClient *gconf;
} RBSpotifyPluginPrivate;

typedef struct
{
     PeasExtensionBase parent;
     RBSpotifyPluginPrivate *privyou;
} RBSpotifyPlugin;

typedef struct
{
     PeasExtensionBaseClass parent_class;
} RBSpotifyPluginClass;

#if 0 
GType	rb_spotify_plugin_get_type		(void) G_GNUC_CONST;
#endif

G_END_DECLS

#endif
