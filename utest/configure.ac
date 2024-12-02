AC_INIT()

test -z "$DEBUG" && DEBUG=1
test -z "$OPT" && OPT=-O3

AC_PROG_MAKE_SET()

AC_ARG_VAR([DEBUG], 'compile with debug symbols.')
AC_ARG_VAR([OPT], 'specify compiler optimization level, like -O3.')
AC_ARG_VAR([TARGET_INC_DIR], 'specify test target include directory.')
AC_ARG_VAR([TARGET_LIB_DIR], 'specify test target lib directory.')
AC_ARG_VAR([TARGET_LIBS], 'specify test target libraries to link against, like -lm.')

ac_default_prefix=./

AC_CONFIG_FILES([
  Makefile
  main.mk
])

AC_OUTPUT()

