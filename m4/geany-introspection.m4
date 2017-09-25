dnl GEANY_CHECK_INTROSPECTION
dnl checks for GtkDoc header generation requirements and define
dnl ENABLE_GTKDOC_HEADER Automake conditional as appropriate
AC_DEFUN([GEANY_CHECK_INTROSPECTION],
[
	AC_REQUIRE([GEANY_CHECK_GTKDOC_HEADER])
	gir_ver=1.30.0

	AC_ARG_ENABLE(introspection,
			  AS_HELP_STRING([--enable-introspection],
							 [Generate GObject-Introspection data [default=auto]]),
							 [arg_enable_introspection=$enableval],
							 [arg_enable_introspection=auto])

	dnl Don't use GOBJECT_INTROSPECTION_CHECK because it's impossible to detect
	dnl if yes or auto was passed to --enable-introspection. Therefore we have our
	dnl own AC_ARG_ENABLE and use GOBJECT_INTROSPECTION_REQUIRE where appropriate.
	m4_ifdef([GOBJECT_INTROSPECTION_REQUIRE],dnl
				 [AS_IF([test $arg_enable_introspection = yes],
							[GOBJECT_INTROSPECTION_REQUIRE([$gir_ver])],
						[test $arg_enable_introspection != no],
							[PKG_CHECK_EXISTS([gobject-introspection-1.0 >= $gir_ver],
								[GOBJECT_INTROSPECTION_REQUIRE([$gir_ver])])])],
				 [AS_IF([test $arg_enable_introspection = xyes],
						[AC_MSG_ERROR([GObject-Introspection support not built in])],
						[AC_MSG_NOTICE([GObject-Introspection support not built in])
						 enable_introspection=no])])

	dnl Warn (and disable) our bail out depending on gtkdoc header availability
	AS_IF([test "$arg_enable_introspection$geany_enable_gtkdoc_header" = "yesno"],
		  [AC_MSG_ERROR([GObject-Introspection support requires --enable-gtkdoc-header])],
		  [test "$enable_introspection$geany_enable_gtkdoc_header" = "yesno"],
		  [AC_MSG_NOTICE([GObject-Introspection support requires --enable-gtkdoc-header])
		   enable_introspection=no])

	AM_CONDITIONAL(HAVE_INTROSPECTION, [test "$enable_introspection" = "yes"])
	GEANY_STATUS_ADD([Generate GObject-Introspection data], [$enable_introspection])
])
