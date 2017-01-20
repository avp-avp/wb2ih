AM_CPPFLAGS = -I../include/
bin_PROGRAMS = wb2ih
wb2ih_SOURCES = wb2ih.cpp WbImperiHomeWebServer.cpp
wb2ih_LDADD =  $(top_builddir)/libs/libcomm/libcomm.la \
	$(top_builddir)/libs/libwb/libwb.la  \
	$(top_builddir)/libs/libutils/libutils.la \
	-lmosquitto  -lmosquittopp -lpthread
