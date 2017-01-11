AM_CPPFLAGS = -iquote $(top_srcdir)

noinst_LTLIBRARIES = libthreadsafe.la
bin_PROGRAMS = threadsafe_test readwritemutex_test

libthreadsafe_la_SOURCES = \
	aithreadid.cxx \
	aithreadid.h

threadsafe_test_SOURCES = \
	aithreadsafe_test.cxx \
	aithreadsafe.h

readwritemutex_test_SOURCES = \
	AIReadWriteMutex_test.cxx \
	AIReadWriteMutex.h

libthreadsafe_la_CXXFLAGS = @LIBCWD_FLAGS@

threadsafe_test_CXXFLAGS = @LIBCWD_FLAGS@
threadsafe_test_LDADD = libthreadsafe.la ../utils/libutils.la ../cwd/libcwd.la @LIBCWD_LIBS@

readwritemutex_test_CXXFLAGS = @LIBCWD_FLAGS@
readwritemutex_test_LDADD = libthreadsafe.la ../utils/libutils.la ../cwd/libcwd.la @LIBCWD_LIBS@

# --------------- Maintainer's Section

if MAINTAINER_MODE
distclean-local:
	rm -f *.s *.ii

clean-local:
endif

MAINTAINERCLEANFILES = $(srcdir)/Makefile.in
