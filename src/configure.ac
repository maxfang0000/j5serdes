AC_INIT()

test -z "$DEBUG" && DEBUG=1
test -z "$OPT" && OPT=-O3

AC_PROG_MAKE_SET()

AC_ARG_VAR([DEBUG], 'compile with debug symbols.')
AC_ARG_VAR([OPT], 'specify compiler optimization level, like -O3.')
AC_ARG_VAR([INSTALL_INC_DIR], 'specify installation directory for includes.')
AC_ARG_VAR([INSTALL_LIB_DIR], 'specify installation directory for libraries.')
AC_ARG_VAR([INSTALL_BIN_DIR], 'specify installation directory for binaries.')

ac_default_prefix=./

AC_CONFIG_FILES([
  Makefile
  main.mk
  apps/subdir.mk
  apps/j5tojson/subdir.mk
])

AC_OUTPUT()

