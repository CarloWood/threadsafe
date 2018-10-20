# threadsafe depends on utils:
m4_if(cwm4_submodule_dirname, [], [m4_append_uniq([CW_SUBMODULE_SUBDIRS], [utils], [ ])])

m4_if(cwm4_submodule_dirname, [], [m4_append_uniq([CW_SUBMODULE_SUBDIRS], cwm4_submodule_basename, [ ])])
m4_append_uniq([CW_SUBMODULE_CONFIG_FILES], cwm4_quote(cwm4_submodule_path[/Makefile]), [ ])

AC_LANG_SAVE
AC_LANG_CPLUSPLUS

# Check for the existence of required boost headers.
AC_CHECK_HEADERS([boost/math/common_factor.hpp], [],
[AC_MSG_ERROR([

You need the boost libraries.
])
])

AC_LANG_RESTORE
