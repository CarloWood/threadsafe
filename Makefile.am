AM_CPPFLAGS = -iquote $(top_builddir) -iquote $(srcdir)/.. -iquote $(top_srcdir) -iquote $(top_srcdir)/cwds

EXTRA_DIST = AIMutex.h AIRecursiveMutex.h Condition.h

noinst_LTLIBRARIES = libthreadsafe.la

libthreadsafe_la_SOURCES = \
	aithreadid.cxx \
	aithreadid.h

libthreadsafe_la_CXXFLAGS = @LIBCWD_R_FLAGS@

# --------------- Maintainer's Section

if MAINTAINER_MODE
distclean-local:
	rm -f *.s *.ii

clean-local:
endif

MAINTAINERCLEANFILES = $(srcdir)/Makefile.in
