dnl Process this file with autoconf to produce a configure script.

AC_PREREQ([2.68])
AC_INIT([rhythmbox-spotify],[1.0])
AC_CONFIG_MACRO_DIR([m4])

AC_CONFIG_HEADERS([config.h])

AM_INIT_AUTOMAKE([1.11])

IT_PROG_INTLTOOL([0.41.0])
AC_PROG_CC

LT_PREREQ([2.2])
LT_INIT

pkg_modules="rhythmbox libspotify libpeas-1.0 libpeas-gtk-1.0 gconf-2.0"
             
PKG_CHECK_MODULES([DEPENDENCIES], [$pkg_modules])

GETTEXT_PACKAGE=rhythmbox-spotify
AC_SUBST(GETTEXT_PACKAGE)
AC_DEFINE_UNQUOTED(GETTEXT_PACKAGE, "$GETTEXT_PACKAGE", [Define to the Gettext package name])

AM_GLIB_GNU_GETTEXT

AC_CONFIG_FILES([
Makefile
src/Makefile
data/Makefile
])

AC_OUTPUT

