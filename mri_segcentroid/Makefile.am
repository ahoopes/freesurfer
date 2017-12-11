## 
## Makefile.am 
##

AM_CXXFLAGS=-std=c++11 -I$(top_srcdir)/include -D$(target_cpu)
AM_LDFLAGS=

bin_PROGRAMS = mri_segcentroid

mri_segcentroid_SOURCES=mri_segcentroid.cpp
# + mri_segcentroid.help.xml.h
mri_segcentroid_LDADD= $(addprefix $(top_builddir)/, $(LIBS_MGH))
mri_segcentroid_LDFLAGS=$(OS_LDFLAGS)

# foodir=$(prefix)/docs/xml
# foo_DATA=mri_segcentroid.help.xml
# foo2dir=$(prefix)/docs/html
# foo2_DATA=mri_segcentroid.help.xml.html

# TESTS=$(top_builddir)/scripts/help_xml_validate

clean-local:
	rm -f $(BUILT_SOURCES) 

EXTRA_DIST=$(foo_DATA) $(BUILT_SOURCES)

# Our release target. Include files to be excluded here. They will be
# found and removed after 'make install' is run during the 'make
# release' target.
EXCLUDE_FILES=""
include $(top_srcdir)/Makefile.extra

# BUILT_SOURCES=mri_segcentroid.help.xml.h mri_segcentroid.help.xml.html
