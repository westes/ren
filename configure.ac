AC_INIT([ren], [1.1.0], [westes575@gmail.com])
AC_CONFIG_SRCDIR([ren.c])
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_MACRO_DIRS([m4])
AC_CONFIG_HEADERS([config.h])
AM_INIT_AUTOMAKE([1.16 foreign])
AC_PROG_CC
AC_CHECK_HEADERS([dirent.h unistd.h])
AC_CHECK_FUNCS([scandir], [], [AC_MSG_ERROR([ren needs scandir(3)])])
AM_GNU_GETTEXT([external])
AM_GNU_GETTEXT_VERSION([0.19])
AC_CONFIG_FILES([Makefile po/Makefile.in])
AC_OUTPUT
