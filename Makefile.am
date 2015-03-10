SUBDIRS = sourdough
AM_CXXFLAGS = -std=c++11 -g 
AM_CPPFLAGS = -I$(srcdir)/sourdouch/src

LDADD = ./sourdough/src/libsourdough.a -lpthread -lpulse -lpulse-simple

bin_PROGRAMS = lsclient lsserver

lsclient_SOURCES = lsclient.cc loudspeaker.hh

lsserver_SOURCES = lsserver.cc loudspeaker.hh

