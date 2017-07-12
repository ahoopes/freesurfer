AM_CFLAGS=-I$(top_srcdir)/include
AM_CXXFLAGS=-I$(top_srcdir)/include

bin_PROGRAMS = devgeodesics
devgeodesics_SOURCES=devgeodesics.c
devgeodesics_LDADD= $(addprefix $(top_builddir)/, $(LIBS_MGH))
devgeodesics_LDFLAGS=$(OS_LDFLAGS)

EXCLUDE_FILES=
include $(top_srcdir)/Makefile.extra
