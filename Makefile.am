AM_CPPFLAGS = -iquote $(top_builddir) -iquote $(srcdir)/.. -iquote $(top_srcdir) -iquote $(top_srcdir)/cwds

EXTRA_DIST = AIMutex.h AIRecursiveMutex.h Condition.h

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

readwritespinlock_test_SOURCES = \
	AIReadWriteSpinLock_test.cxx \
	AIReadWriteSpinLock.h

libthreadsafe_la_CXXFLAGS = -std=c++11 @LIBCWD_R_FLAGS@

threadsafe_test_CXXFLAGS = -std=c++11 @LIBCWD_R_FLAGS@
threadsafe_test_LDADD = libthreadsafe.la ../utils/libutils_r.la $(top_builddir)/cwds/libcwds_r.la

readwritemutex_test_CXXFLAGS = -std=c++11 @LIBCWD_R_FLAGS@
readwritemutex_test_LDADD = libthreadsafe.la ../utils/libutils_r.la $(top_builddir)/cwds/libcwds_r.la

readwritespinlock_test_CXXFLAGS = -std=c++11 @LIBCWD_R_FLAGS@
readwritespinlock_test_LDADD = libthreadsafe.la ../utils/libutils_r.la $(top_builddir)/cwds/libcwds_r.la

# --------------- Maintainer's Section

if MAINTAINER_MODE
distclean-local:
	rm -f *.s *.ii

clean-local:
endif

MAINTAINERCLEANFILES = $(srcdir)/Makefile.in
