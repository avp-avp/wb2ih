AC_INIT([wb2ih], [0.1], [avp@avp.name])
AM_INIT_AUTOMAKE([-Wall -Werror foreign])

AC_LANG([C++])
AM_PROG_AR
#AC_PROG_CXX
#AM_PROG_LIBTOOL
LT_INIT
AC_CONFIG_MACRO_DIR([m4])

AC_CHECK_HEADERS([json/json.h])
AC_CHECK_HEADERS([jsoncpp/json/json.h])
AM_CONFIG_HEADER(haconfig.inc)

AC_DEFINE([WB2IH],[1],[Wiren board imperihome interface])
AC_DEFINE([USE_CONFIG], [1], [Use JSON Config])
AC_SEARCH_LIBS([_ZNK4Json5Value4sizeEv], [jsoncpp], [], [])

AC_CONFIG_FILES([
 Makefile
 libs/libutils/Makefile
 libs/libcomm/Makefile
 libs/libwb/Makefile
 tests/Makefile
 wb2ih/Makefile
])
AC_OUTPUT