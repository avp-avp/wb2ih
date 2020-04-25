AC_INIT([wb2ih], [0.1], [avp@avp.name])
AM_INIT_AUTOMAKE([-Wall -Werror foreign])

AC_PROG_CXX
AC_LANG_PUSH([C++])
AM_PROG_AR

CXXFLAGS="$CXXFLAGS -std=c++0x"

AC_COMPILE_IFELSE([AC_LANG_SOURCE(
  [[template <typename T>
    struct check
    {
      static_assert(sizeof(int) <= sizeof(T), "not big enough");
    };

    typedef check<check<bool>> right_angle_brackets;

    int a;
    decltype(a) b;

    typedef check<int> check_type;
    check_type c;
    check_type&& cr = static_cast<check_type&&>(c);]])],,
  AC_MSG_FAILURE(['$CXX $CXXFLAGS' does not accept ISO C++11]))

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
