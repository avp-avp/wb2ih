AM_CPPFLAGS = -I../include/
bin_PROGRAMS = wb2ihtest
check_PROGRAMS = wb2ihtest
wb2ihtest_SOURCES = main.cpp
wb2ihtest_LDADD = $(top_builddir)/libs/libutils/libutils.la \
	$(top_builddir)/libs/libwb/libwb.la  \
	-lmosquitto  -lmosquittopp -lpthread

AM_CFLAGS = -pthread
AM_CXXFLAGS = -pthread

