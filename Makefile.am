ACLOCAL_AMFLAGS = -I m4
SUBDIRS = po

bin_PROGRAMS = ren
ren_SOURCES = ren.c
ren_CPPFLAGS = -DLOCALEDIR=\"$(localedir)\"
ren_LDADD = $(LIBINTL)

dist_man_MANS = ren.1

EXTRA_DIST = autogen.sh build-aux/config.rpath
