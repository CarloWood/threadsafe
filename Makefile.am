EXTRA_DIST = *.h

noinst_LTLIBRARIES = libthreadsafe.la
bin_PROGRAMS = threadsafe_test readwritemutex_test

# See src/Makefile.am
DEFS = @DEFS@
DEFAULT_INCLUDES=-iquote $(top_builddir) -iquote $(top_builddir)/src -iquote $(srcdir) -iquote $(srcdir)/.. -iquote $(srcdir)/../cwd

# See src/Makefile.am.
libthreadsafe_la_CXXFLAGS = @CXXFLAGS@ @LIBCWD_FLAGS@
libthreadsafe_la_SOURCES = \
	aithreadid.cpp

threadsafe_test_SOURCES = \
	threadsafe_test.cpp

readwritemutex_test_SOURCES = \
	readwritemutex_test.cpp

threadsafe_test_CXXFLAGS = @CXXFLAGS@ @LIBCWD_FLAGS@
threadsafe_test_LDFLAGS =
threadsafe_test_LDADD = ../utils/.libs/libutils.a ../cwd/.libs/libdebug.a @LIBS@ @LIBCWD_LIBS@
threadsafe_test_DEPENDENCIES = ../utils/libutils.la ../cwd/libdebug.la

readwritemutex_test_CXXFLAGS = @CXXFLAGS@ @LIBCWD_FLAGS@
readwritemutex_test_LDFLAGS =
readwritemutex_test_LDADD = ../utils/.libs/libutils.a ../cwd/.libs/libdebug.a @LIBS@ @LIBCWD_LIBS@
readwritemutex_test_DEPENDENCIES = ../utils/libutils.la ../cwd/libdebug.la

MAINTAINERCLEANFILES = $(srcdir)/Makefile.in

if MAINTAINER_MODE
distclean-local:
	rm -f *.s *.ii

clean-local:
endif
