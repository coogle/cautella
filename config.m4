dnl $Id$
dnl config.m4 for extension cautela

dnl Comments in this file start with the string 'dnl'.
dnl Remove where necessary. This file will not work
dnl without editing.

dnl If your extension references something external, use with:

PHP_ARG_ENABLE(cautela, whether to enable cautela support,
[  --enable-cautela           Enable cautela support])

if test "$PHP_CAUTELA" != "no"; then
  PHP_NEW_EXTENSION(cautela, cautela.c, $ext_shared)
fi
