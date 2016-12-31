AM_CPPFLAGS=-iquote $(top_srcdir)

noinst_LTLIBRARIES = libthreadsafe.la
bin_PROGRAMS = threadsafe_test readwritemutex_test

libthreadsafe_la_CXXFLAGS = @LIBCWD_FLAGS@
libthreadsafe_la_SOURCES = \
	aithreadid.cpp \
	aithreadid.h

threadsafe_test_SOURCES = \
	aithreadsafe_test.cpp \
	aithreadsafe.h

readwritemutex_test_SOURCES = \
	AIReadWriteMutex_test.cpp \
	AIReadWriteMutex.h

threadsafe_test_CXXFLAGS = @LIBCWD_FLAGS@
threadsafe_test_LDADD = ../utils/libutils.la ../cwd/libdebug.la @LIBCWD_LIBS@

readwritemutex_test_CXXFLAGS = @LIBCWD_FLAGS@
readwritemutex_test_LDADD = ../utils/libutils.la ../cwd/libdebug.la @LIBCWD_LIBS@

# --------------- Maintainer's Section

if MAINTAINER_MODE
distclean-local:
	rm -f *.s *.ii

clean-local:
endif

MAINTAINERCLEANFILES = $(srcdir)/Makefile.in
