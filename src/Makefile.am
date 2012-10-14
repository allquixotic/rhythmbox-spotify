plugindir = ${libdir}/rhythmbox/plugins/spotify

plugin_LTLIBRARIES = libspotify.la

libspotify_la_SOURCES = audio.c		   \
						appkey.c		   \
					    rb-spotify-src.c \
			  		    rb-spotify-source.c \
						rb-spotify-plugin.c

libspotify_la_LIBADD = $(DEPENDENCIES_LIBS)								  
libspotify_la_CFLAGS = $(DEPENDENCIES_CFLAGS)
libspotify_la_LDFLAGS = -module -avoid-version
libspotify_la_LIBTOOLFLAGS = --tag=disable-static

plugin_in_files = spotify.plugin.in

%.plugin: %.plugin.in $(INTLTOOL_MERGE) $(wildcard $(top_srcdir)/po/*po) ; $(INTLTOOL_MERGE) $(top_srcdir)/po $< $@ -d -u -c $(top_builddir)/po/.intltool-merge-cache

BUILT_SOURCES =							\
	$(plugin_in_files:.plugin.in=.plugin) 		\
	$(NULL)
	
plugin_DATA = 			\
	$(BUILT_SOURCES)	\
	$(NULL)

EXTRA_DIST = 			\
	$(uixml_DATA)		\
	$(plugin_in_files)	\
	$(NULL)

CLEANFILES = 			\
	$(BUILT_SOURCES)	\
	$(NULL)

DISTCLEANFILES =		\
	$(BUILT_SOURCES)	\
	$(NULL)


								  
