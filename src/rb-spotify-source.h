/*
 * Copyright/Licensing information.
 */

/* inclusion guard */
#ifndef __RBSPOTIFYSOURCE_H__
#define __RBSPOTIFYSOURCE_H__

#include "config.h"

#include <glib-object.h>
#include <libspotify/api.h>

/*
 * Potentially, include other headers on which this header depends.
 */
#include <rhythmbox/shell/rb-shell.h>
#include <rhythmbox/sources/rb-browser-source.h>
#include <rhythmbox/plugins/rb-plugin-macros.h>

G_BEGIN_DECLS

/*
 * Type macros.
 */
#define RBSPOTIFYSOURCE_TYPE                  (rbspotifysource_get_type ())
#define RBSPOTIFYSOURCE(obj)                  (G_TYPE_CHECK_INSTANCE_CAST ((obj), RBSPOTIFYSOURCE_TYPE, RBSpotifySource))
#define IS_RBSPOTIFYSOURCE(obj)               (G_TYPE_CHECK_INSTANCE_TYPE ((obj), RBSPOTIFYSOURCE_TYPE))
#define RBSPOTIFYSOURCE_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), RBSPOTIFYSOURCE_TYPE, RBSpotifySourceClass))
#define IS_RBSPOTIFYSOURCE_CLASS(klass)       (G_TYPE_CHECK_CLASS_TYPE ((klass),  RBSPOTIFYSOURCE_TYPE))
#define RBSPOTIFYSOURCE_GET_CLASS(obj)        (G_TYPE_INSTANCE_GET_CLASS ((obj),  RBSPOTIFYSOURCE_TYPE, RBSpotifySourceClass))

typedef struct _RBSpotifySourcePrivate
{
	sp_session *sess;
	RhythmDB *db;
	RhythmDBEntryType *type;
} RBSpotifySourcePrivate;

typedef struct
{
	RBBrowserSource parent;

	RBSpotifySourcePrivate *priv;
} RBSpotifySource;

typedef struct
{
	RBBrowserSourceClass parent;
} RBSpotifySourceClass;

/* used by MAMAN_TYPE_BAR */
GType
rbspotifysource_get_type(void);

/*
 * Method definitions.
 */

G_END_DECLS

#endif /* __MAMAN_BAR_H__ */
