# evolution/acinclude.m4
# shared configure.in hacks between Evolution and Connector


# EVO_PURIFY_SUPPORT
# Add --enable-purify. If the user turns it on, subst PURIFY and set
# the automake conditional ENABLE_PURIFY
AC_DEFUN([EVO_PURIFY_SUPPORT], [
	AC_ARG_ENABLE(purify, 
	[  --enable-purify=[no/yes]      Enable support for building executables with Purify.],,enable_purify=no)
	AC_PATH_PROG(PURIFY, purify, impure)
	AC_ARG_WITH(purify-options, [  --with-purify-options=OPTIONS      Options passed to the purify command line (defaults to PURIFYOPTIONS variable).])
	if test "x$with_purify_options" = "xno"; then
		with_purify_options="-always-use-cache-dir=yes -cache-dir=/gnome/lib/purify"
	fi
	if test "x$PURIFYOPTIONS" = "x"; then
		PURIFYOPTIONS=$with_purify_options
	fi
	AC_SUBST(PURIFY)
	AM_CONDITIONAL(ENABLE_PURIFY, test "x$enable_purify" = "xyes" -a "x$PURIFY" != "ximpure")
	PURIFY="$PURIFY $PURIFYOPTIONS"
])


# EVO_LDAP_CHECK(default)
# Add --with-openldap and --with-static-ldap options. --with-openldap
# defaults to the given value if not specified. If LDAP support is
# configured, HAVE_LDAP will be defined and the automake conditional
# ENABLE_LDAP will be set. LDAP_CFLAGS and LDAP_LIBS will be set
# appropriately.
AC_DEFUN([EVO_LDAP_CHECK], [
	default="$1"

	AC_ARG_WITH(openldap,     [  --with-openldap=[no/yes/PREFIX]      Enable LDAP support in evolution])
	AC_ARG_WITH(static-ldap,  [  --with-static-ldap=[no/yes]          Link LDAP support statically into evolution ])
	AC_CACHE_CHECK([for OpenLDAP], ac_cv_with_openldap, ac_cv_with_openldap="${with_openldap:=$default}")
	case $ac_cv_with_openldap in
	no|"")
		with_openldap=no
		;;
	yes)
		with_openldap=/usr
		;;
	*)
		with_openldap=$ac_cv_with_openldap
		LDAP_CFLAGS="-I$ac_cv_with_openldap/include"
		LDAP_LDFLAGS="-L$ac_cv_with_openldap/lib"
		;;
	esac

	if test "$with_openldap" != no; then
		AC_DEFINE(HAVE_LDAP,1,[Define if you have LDAP support])

		case $with_static_ldap in
		no|"")
			with_static_ldap=no
			;;
		*)
			with_static_ldap=yes
			;;
		esac

		AC_CACHE_CHECK(if OpenLDAP is version 2.x, ac_cv_openldap_version2, [
			CPPFLAGS_save="$CPPFLAGS"
			CPPFLAGS="$CPPFLAGS $LDAP_CFLAGS"
			AC_EGREP_CPP(yes, [
				#include "ldap.h"
				#if LDAP_VENDOR_VERSION > 20000
				yes
				#endif
			], ac_cv_openldap_version2=yes, ac_cv_openldap_version2=no)
			CPPFLAGS="$CPPFLAGS_save"
		])
		if test "$ac_cv_openldap_version2" = no; then
			AC_MSG_ERROR(evolution requires OpenLDAP version >= 2)
		fi

		AC_CHECK_LIB(resolv, res_query, LDAP_LIBS="-lresolv")
		AC_CHECK_LIB(socket, bind, LDAP_LIBS="$LDAP_LIBS -lsocket")
		AC_CHECK_LIB(nsl, gethostbyaddr, LDAP_LIBS="$LDAP_LIBS -lnsl")
		AC_CHECK_LIB(lber, ber_get_tag, [
			if test "$with_static_ldap" = "yes"; then
				LDAP_LIBS="$with_openldap/lib/liblber.a $LDAP_LIBS"

				# libldap might depend on OpenSSL... We need to pull
				# in the dependency libs explicitly here since we're
				# not using libtool for the configure test.
				if test -f $with_openldap/lib/libldap.la; then
					LDAP_LIBS="`. $with_openldap/lib/libldap.la; echo $dependency_libs` $LDAP_LIBS"
				fi
			else
				LDAP_LIBS="-llber $LDAP_LIBS"
			fi
			AC_CHECK_LIB(ldap, ldap_open, [
					if test $with_static_ldap = "yes"; then
						LDAP_LIBS="$with_openldap/lib/libldap.a $LDAP_LIBS"
					else
						LDAP_LIBS="-lldap $LDAP_LIBS"
					fi],
				LDAP_LIBS="", $LDAP_LDFLAGS $LDAP_LIBS)
			LDAP_LIBS="$LDAP_LDFLAGS $LDAP_LIBS"
		], LDAP_LIBS="", $LDAP_LDFLAGS $LDAP_LIBS)

		if test -z "$LDAP_LIBS"; then
			AC_MSG_ERROR(could not find OpenLDAP libraries)
		fi

		AC_SUBST(LDAP_CFLAGS)
		AC_SUBST(LDAP_LIBS)
	fi
	AM_CONDITIONAL(ENABLE_LDAP, test $with_openldap != no)
])

# EVO_SUNLDAP_CHECK
# Add --with-sunldap and --with-static-sunldap options. --with-sunldap
# defaults to the given value if not specified. If LDAP support is
# configured, HAVE_LDAP will be defined and the automake conditional
# ENABLE_LDAP will be set. LDAP_CFLAGS and LDAP_LIBS will be set
# appropriately, and --with-sunldap and --with-openldap is mutually exclusive.
AC_DEFUN([EVO_SUNLDAP_CHECK], [
        default="$1"

        AC_ARG_WITH(sunldap,     [  --with-sunldap=[no/yes/PREFIX]      Enable SunLDAP support in evolution])
        AC_ARG_WITH(static-sunldap,  [  --with-static-sunldap=[no/yes]          Link SunLDAP support statically into evolution ])
        AC_CACHE_CHECK([for SunLDAP], ac_cv_with_sunldap, ac_cv_with_sunldap="${with_sunldap:=$default}")
        case $ac_cv_with_sunldap in
        no|"")
                with_sunldap=no
                ;;
        yes)
                with_sunldap=/usr
                ;;
        *)
                with_sunldap=$ac_cv_with_sunldap
                LDAP_CFLAGS="-I$ac_cv_with_sunldap/include"
                LDAP_LDFLAGS="-L$ac_cv_with_sunldap/lib"
                ;;
        esac

        if test "$with_sunldap" != no; then
                AC_DEFINE(HAVE_LDAP,1,[Define if you have LDAP support])
                AC_DEFINE(SUNLDAP, 1, [Define if you use SunLDAP])

                case $with_static_sunldap in
                no|"")
                        with_static_sunldap=no
                        ;;
                *)
                        with_static_sunldap=yes
                        ;;
                esac

                AC_CACHE_CHECK(if SunLDAP is version 2.x, ac_cv_sunldap_version2, [
                        CPPFLAGS_save="$CPPFLAGS"
                        CPPFLAGS="$CPPFLAGS $LDAP_CFLAGS"
                        AC_EGREP_CPP(yes, [
                                #include "ldap.h"
                                #if LDAP_VENDOR_VERSION >= 500
                                yes
                                #endif
                        ], ac_cv_sunldap_version2=yes, ac_cv_sunldap_version2=no)
                        CPPFLAGS="$CPPFLAGS_save"
                ])
                if test "$ac_cv_sunldap_version2" = no; then
                       AC_MSG_ERROR(evolution requires SunLDAP version >= 2)
               fi

                AC_CHECK_LIB(resolv, res_query, LDAP_LIBS="-lresolv")
                AC_CHECK_LIB(socket, bind, LDAP_LIBS="$LDAP_LIBS -lsocket")
                AC_CHECK_LIB(nsl, gethostbyaddr, LDAP_LIBS="$LDAP_LIBS -lnsl")
                AC_CHECK_LIB(ldap, ldap_open, [
                        if test $with_static_sunldap = "yes"; then
                                LDAP_LIBS="$with_sunldap/lib/libldap.a $LDAP_LIBS"
                        else
                                LDAP_LIBS="-lldap $LDAP_LIBS"
                        fi
                        if test `uname -s` != "SunOS" ; then
                                AC_CHECK_LIB(lber, ber_get_tag, [
                                        if test "$with_static_sunldap" = "yes"; then
                                                LDAP_LIBS="$with_sunldap/lib/liblber.a $LDAP_LIBS"
                                                # libldap might depend on OpenSSL... We need to pull
                                                # in the dependency libs explicitly here since we're
                                                # not using libtool for the configure test.
                                                if test -f $with_sunldap/lib/libldap.la; then
                                                        LDAP_LIBS="`. $with_sunldap/lib/libldap.la; echo $dependency_libs` $LDAP_LIBS"
                                                fi
                                        else
                                                LDAP_LIBS="-llber $LDAP_LIBS"
                                        fi], LDAP_LIBS="", $LDAP_LDFLAGS $LDAP_LIBS)
                        fi
                        LDAP_LIBS="$LDAP_LDFLAGS $LDAP_LIBS"
                ], LDAP_LIBS="", $LDAP_LDFLAGS $LDAP_LIBS)

                if test -z "$LDAP_LIBS"; then
                       AC_MSG_ERROR(could not find SunLDAP libraries)
                fi

                AC_SUBST(LDAP_CFLAGS)
                AC_SUBST(LDAP_LIBS)
        fi
        AM_CONDITIONAL(ENABLE_LDAP, test $with_sunldap != no)
])

# EVO_PTHREAD_CHECK
AC_DEFUN([EVO_PTHREAD_CHECK],[
	PTHREAD_LIB=""
	AC_CHECK_LIB(pthread, pthread_create, PTHREAD_LIB="-lpthread",
		[AC_CHECK_LIB(pthreads, pthread_create, PTHREAD_LIB="-lpthreads",
		    [AC_CHECK_LIB(c_r, pthread_create, PTHREAD_LIB="-lc_r",
			[AC_CHECK_LIB(pthread, __pthread_attr_init_system, PTHREAD_LIB="-lpthread",
				[AC_CHECK_FUNC(pthread_create)]
			)]
		    )]
		)]
	)
	AC_SUBST(PTHREAD_LIB)
	AC_MSG_CHECKING([if pthread_t can be cast to a guint64 without loss of data])
	CFLAGS_save="$CFLAGS"
	CFLAGS="$CFLAGS $GLIB_CFLAGS"
	AC_TRY_COMPILE(
		[#include <pthread.h>
		 #include <glibconfig.h>],
		[char a[(sizeof(guint64)>=sizeof(pthread_t))?1:-1];
		 guint64 l;
		 pthread_t t;
		 l = (guint64) t;],
		[AC_DEFINE(HAVE_GUINT64_CASTABLE_PTHREAD_T,1,[Define to 1 if pthread_t can be cast to a guint64])
		 AC_MSG_RESULT([yes])],
		[AC_MSG_RESULT([no])]
	)
	CFLAGS="$CFLAGS_save"
	AC_PROVIDE([EVO_PTHREAD_CHECK])
])
dnl -*- mode: autoconf -*-

# serial 1

dnl Usage:
dnl   GTK_DOC_CHECK([minimum-gtk-doc-version])
AC_DEFUN([GTK_DOC_CHECK],
[
  AC_BEFORE([AC_PROG_LIBTOOL],[$0])dnl setup libtool first
  AC_BEFORE([AM_PROG_LIBTOOL],[$0])dnl setup libtool first
  dnl for overriding the documentation installation directory
  AC_ARG_WITH(html-dir,
    AC_HELP_STRING([--with-html-dir=PATH], [path to installed docs]),,
    [with_html_dir='${datadir}/gtk-doc/html'])
  HTML_DIR="$with_html_dir"
  AC_SUBST(HTML_DIR)

  dnl enable/disable documentation building
  AC_ARG_ENABLE(gtk-doc,
    AC_HELP_STRING([--enable-gtk-doc],
                   [use gtk-doc to build documentation [default=no]]),,
    enable_gtk_doc=no)

  have_gtk_doc=no
  if test -z "$PKG_CONFIG"; then
    AC_PATH_PROG(PKG_CONFIG, pkg-config, no)
  fi
  if test "$PKG_CONFIG" != "no" && $PKG_CONFIG --exists gtk-doc; then
    have_gtk_doc=yes
  fi

  dnl do we want to do a version check?
ifelse([$1],[],,
  [gtk_doc_min_version=$1
  if test "$have_gtk_doc" = yes; then
    AC_MSG_CHECKING([gtk-doc version >= $gtk_doc_min_version])
    if $PKG_CONFIG --atleast-version $gtk_doc_min_version gtk-doc; then
      AC_MSG_RESULT(yes)
    else
      AC_MSG_RESULT(no)
      have_gtk_doc=no
    fi
  fi
])
  if test x$enable_gtk_doc = xyes; then
    if test "$have_gtk_doc" != yes; then
      enable_gtk_doc=no
    fi
  fi

  AM_CONDITIONAL(ENABLE_GTK_DOC, test x$enable_gtk_doc = xyes)
  AM_CONDITIONAL(GTK_DOC_USE_LIBTOOL, test -n "$LIBTOOL")
])


dnl dolt, a replacement for libtool
dnl Copyright © 2007-2008 Josh Triplett <josh@freedesktop.org>
dnl Copying and distribution of this file, with or without modification,
dnl are permitted in any medium without royalty provided the copyright
dnl notice and this notice are preserved.
dnl
dnl To use dolt, invoke the DOLT macro immediately after the libtool macros.
dnl Optionally, copy this file into acinclude.m4, to avoid the need to have it
dnl installed when running autoconf on your project.

AC_DEFUN([DOLT], [
AC_REQUIRE([AC_CANONICAL_HOST])
# dolt, a replacement for libtool
# Josh Triplett <josh@freedesktop.org>
AC_PATH_PROG(DOLT_BASH, bash)
AC_MSG_CHECKING([if dolt supports this host])
dolt_supported=yes
if test x$DOLT_BASH = x; then
    dolt_supported=no
fi
if test x$GCC != xyes; then
    dolt_supported=no
fi
case $host in
i?86-*-linux*|x86_64-*-linux*|powerpc-*-linux* \
|amd64-*-freebsd*|i?86-*-freebsd*|ia64-*-freebsd*)
    pic_options='-fPIC'
    ;;
i?86-apple-darwin*)
    pic_options='-fno-common'
    ;;
*)
    dolt_supported=no
    ;;
esac
if test x$dolt_supported = xno ; then
    AC_MSG_RESULT([no, falling back to libtool])
    LTCOMPILE='$(LIBTOOL) --tag=CC $(AM_LIBTOOLFLAGS) $(LIBTOOLFLAGS) --mode=compile $(COMPILE)'
    LTCXXCOMPILE='$(LIBTOOL) --tag=CXX $(AM_LIBTOOLFLAGS) $(LIBTOOLFLAGS) --mode=compile $(CXXCOMPILE)'
else
    AC_MSG_RESULT([yes, replacing libtool])

dnl Start writing out doltcompile.
    cat <<__DOLTCOMPILE__EOF__ >doltcompile
#!$DOLT_BASH
__DOLTCOMPILE__EOF__
    cat <<'__DOLTCOMPILE__EOF__' >>doltcompile
args=("$[]@")
for ((arg=0; arg<${#args@<:@@@:>@}; arg++)) ; do
    if test x"${args@<:@$arg@:>@}" = x-o ; then
        objarg=$((arg+1))
        break
    fi
done
if test x$objarg = x ; then
    echo 'Error: no -o on compiler command line' 1>&2
    exit 1
fi
lo="${args@<:@$objarg@:>@}"
obj="${lo%.lo}"
if test x"$lo" = x"$obj" ; then
    echo "Error: libtool object file name \"$lo\" does not end in .lo" 1>&2
    exit 1
fi
objbase="${obj##*/}"
__DOLTCOMPILE__EOF__

dnl Write out shared compilation code.
    if test x$enable_shared = xyes; then
        cat <<'__DOLTCOMPILE__EOF__' >>doltcompile
libobjdir="${obj%$objbase}.libs"
if test ! -d "$libobjdir" ; then
    mkdir_out="$(mkdir "$libobjdir" 2>&1)"
    mkdir_ret=$?
    if test "$mkdir_ret" -ne 0 && test ! -d "$libobjdir" ; then
	echo "$mkdir_out" 1>&2
        exit $mkdir_ret
    fi
fi
pic_object="$libobjdir/$objbase.o"
args@<:@$objarg@:>@="$pic_object"
__DOLTCOMPILE__EOF__
    cat <<__DOLTCOMPILE__EOF__ >>doltcompile
"\${args@<:@@@:>@}" $pic_options -DPIC || exit \$?
__DOLTCOMPILE__EOF__
    fi

dnl Write out static compilation code.
dnl Avoid duplicate compiler output if also building shared objects.
    if test x$enable_static = xyes; then
        cat <<'__DOLTCOMPILE__EOF__' >>doltcompile
non_pic_object="$obj.o"
args@<:@$objarg@:>@="$non_pic_object"
__DOLTCOMPILE__EOF__
        if test x$enable_shared = xyes; then
            cat <<'__DOLTCOMPILE__EOF__' >>doltcompile
"${args@<:@@@:>@}" >/dev/null 2>&1 || exit $?
__DOLTCOMPILE__EOF__
        else
            cat <<'__DOLTCOMPILE__EOF__' >>doltcompile
"${args@<:@@@:>@}" || exit $?
__DOLTCOMPILE__EOF__
        fi
    fi

dnl Write out the code to write the .lo file.
dnl The second line of the .lo file must match "^# Generated by .*libtool"
    cat <<'__DOLTCOMPILE__EOF__' >>doltcompile
{
echo "# $lo - a libtool object file"
echo "# Generated by doltcompile, not libtool"
__DOLTCOMPILE__EOF__

    if test x$enable_shared = xyes; then
        cat <<'__DOLTCOMPILE__EOF__' >>doltcompile
echo "pic_object='.libs/${objbase}.o'"
__DOLTCOMPILE__EOF__
    else
        cat <<'__DOLTCOMPILE__EOF__' >>doltcompile
echo pic_object=none
__DOLTCOMPILE__EOF__
    fi

    if test x$enable_static = xyes; then
        cat <<'__DOLTCOMPILE__EOF__' >>doltcompile
echo "non_pic_object='${objbase}.o'"
__DOLTCOMPILE__EOF__
    else
        cat <<'__DOLTCOMPILE__EOF__' >>doltcompile
echo non_pic_object=none
__DOLTCOMPILE__EOF__
    fi

    cat <<'__DOLTCOMPILE__EOF__' >>doltcompile
} > "$lo"
__DOLTCOMPILE__EOF__

dnl Done writing out doltcompile; substitute it for libtool compilation.
    chmod +x doltcompile
    LTCOMPILE='$(top_builddir)/doltcompile $(COMPILE)'
    LTCXXCOMPILE='$(top_builddir)/doltcompile $(CXXCOMPILE)'

dnl automake ignores LTCOMPILE and LTCXXCOMPILE when it has separate CFLAGS for
dnl a target, so write out a libtool wrapper to handle that case.
dnl Note that doltlibtool does not handle inferred tags or option arguments
dnl without '=', because automake does not use them.
    cat <<__DOLTLIBTOOL__EOF__ > doltlibtool
#!$DOLT_BASH
__DOLTLIBTOOL__EOF__
    cat <<'__DOLTLIBTOOL__EOF__' >>doltlibtool
top_builddir_slash="${0%%doltlibtool}"
: ${top_builddir_slash:=./}
args=()
modeok=false
tagok=false
for arg in "$[]@"; do
    case "$arg" in
        --mode=compile) modeok=true ;;
        --tag=CC|--tag=CXX) tagok=true ;;
        *) args+=("$arg")
    esac
done
if $modeok && $tagok ; then
    . ${top_builddir_slash}doltcompile "${args@<:@@@:>@}"
else
    exec ${top_builddir_slash}libtool "$[]@"
fi
__DOLTLIBTOOL__EOF__

dnl Done writing out doltlibtool; substitute it for libtool.
    chmod +x doltlibtool
    LIBTOOL='$(top_builddir)/doltlibtool'
fi
AC_SUBST(LTCOMPILE)
AC_SUBST(LTCXXCOMPILE)
# end dolt
])
