# ============================================================================
# bitcoin_qt.m4 — Qt 6 detection for the Cryptonite build.
# ============================================================================
#
# Detects Qt 6 (Qt6Core, Qt6Gui, Qt6Widgets, Qt6Network, Qt6Test, Qt6DBus)
# via pkg-config and exposes the standard substitutions used by the GUI
# build rules in src/qt/Makefile.am and src/qt/test/Makefile.am:
#
#   bitcoin_enable_qt              yes / no
#   bitcoin_enable_qt_test         yes / no
#   QT_INCLUDES, QT_LIBS           pkg-config CFLAGS / LIBS for the Qt6 modules
#   QT_TEST_INCLUDES, QT_TEST_LIBS Qt6Test
#   QT_DBUS_INCLUDES, QT_DBUS_LIBS Qt6DBus (if found)
#   QT_LDFLAGS                     additional linker flags (e.g. -Wl,--no-undefined)
#   MOC, UIC, RCC, LUPDATE, LRELEASE tool paths
#
# This replaces the stub-only version that disabled the GUI entirely. The
# GUI build is gated by --with-gui (set automatically when pkg-config
# finds the modules).

AC_DEFUN([BITCOIN_QT_INIT],[
  bitcoin_enable_qt=no
  bitcoin_enable_qt_test=no

  if test x$use_pkgconfig = xyes; then
    if test x$PKG_CONFIG = x; then
      AC_MSG_ERROR([pkg-config is required to detect Qt6. Install pkg-config or pass --without-gui.])
    fi

    QT6_PC_MODULES="Qt6Core Qt6Gui Qt6Widgets Qt6Network"
    PKG_CHECK_MODULES([QT], [$QT6_PC_MODULES], [
      bitcoin_enable_qt=yes
      bitcoin_enable_qt_test=yes
      QT_INCLUDES="$QT_CFLAGS"
      QT_LIBS="$QT_LIBS"
      QT_VERSION=`$PKG_CONFIG --modversion Qt6Core 2>/dev/null`
      AC_DEFINE([HAVE_QT], [1], [Define if Qt is present and supported])
    ], [
      if test x$with_qt = xyes; then
        AC_MSG_ERROR([Qt6 not found via pkg-config. Install qt6-base-dev (Debian/Ubuntu) or pass --without-gui.])
      fi
    ])

    if test x$bitcoin_enable_qt_test = xyes; then
      PKG_CHECK_MODULES([QT_TEST], [Qt6Test], [
        QT_TEST_INCLUDES="$QT_TEST_CFLAGS"
        QT_TEST_LIBS="$QT_TEST_LIBS"
      ], [bitcoin_enable_qt_test=no])
    fi

    if test x$bitcoin_enable_qt = xyes; then
      PKG_CHECK_MODULES([QT_LDBUS], [Qt6DBus], [
        QT_DBUS_INCLUDES="$QT_LDBUS_CFLAGS"
        QT_DBUS_LIBS="$QT_LDBUS_LIBS"
        AC_DEFINE([USE_DBUS], [1], [Define if Qt D-Bus is available])
      ], [QT_DBUS_LIBS=])
    fi

    if test x$bitcoin_enable_qt = xyes; then
      QT_LIBEXECDIR=`$PKG_CONFIG --variable=libexecdir Qt6Core`
      QT_BINDIR=`$PKG_CONFIG --variable=bindir Qt6Core`
      # Qt 6 ships most tools via libexec (moc, uic, rcc, qmake, qdbus)
      # but linguist tools (lupdate, lrelease) via bin. Probe both names
      # at every absolute location, then fall back to PATH. A real Qt 6
      # binary must live under /usr/lib/qt6/ once symlinks are resolved;
      # anything else is a qtchooser shim, which would route to whichever
      # Qt version the user happens to have configured (often Qt 5.15 on
      # Ubuntu 26.04). The helper shell function lives in a sibling file
      # so autoconf's m4 layer does not strip its positional parameters.
      m4_esyscmd([cat build-aux/m4/qt6_tool_find.sh])
      MOC=`pick_qt6_tool moc6 moc`
      UIC=`pick_qt6_tool uic6 uic`
      RCC=`pick_qt6_tool rcc6 rcc`
      LUPDATE=`pick_qt6_tool lupdate6 lupdate`
      LRELEASE=`pick_qt6_tool lrelease6 lrelease`

      if test x"$MOC" = xno; then AC_MSG_ERROR([Qt6 moc not found. Install qt6-tools-dev-tools.]); fi
      if test x"$UIC" = xno; then AC_MSG_ERROR([Qt6 uic not found. Install qt6-tools-dev-tools.]); fi
      if test x"$RCC" = xno; then AC_MSG_ERROR([Qt6 rcc not found. Install qt6-tools-dev-tools.]); fi
    fi

    QT_LDFLAGS="-Wl,--no-undefined"
  else
    if test x$with_qt = xyes; then
      AC_MSG_ERROR([--with-gui requires --with-pkgconfig. Re-run configure with --with-pkgconfig.])
    fi
  fi

  AM_CONDITIONAL([BUILD_QT], [test x$bitcoin_enable_qt = xyes])
  AM_CONDITIONAL([BUILD_QT_TEST], [test x$bitcoin_enable_qt_test = xyes])

  AC_SUBST(QT_INCLUDES)
  AC_SUBST(QT_LIBS)
  AC_SUBST(QT_VERSION)
  AC_SUBST(QT_LDFLAGS)
  AC_SUBST(QT_TEST_INCLUDES)
  AC_SUBST(QT_TEST_LIBS)
  AC_SUBST(QT_DBUS_INCLUDES)
  AC_SUBST(QT_DBUS_LIBS)
  AC_SUBST(MOC)
  AC_SUBST(UIC)
  AC_SUBST(RCC)
  AC_SUBST(LUPDATE)
  AC_SUBST(LRELEASE)

  BITCOIN_QT_TEST_QT=no
  if test x$bitcoin_enable_qt_test = xyes -a x$use_tests = xyes; then
    BITCOIN_QT_TEST_QT=yes
  fi
  AM_CONDITIONAL([BUILD_TEST_QT], [test x$BITCOIN_QT_TEST_QT = xyes])
])

# Legacy no-op aliases. Older configure.ac revisions invoked these names; we
# keep them as harmless stubs so any leftover m4_include references resolve.
AC_DEFUN([BITCOIN_QT_CHECK],[])
AC_DEFUN([BITCOIN_QT_PATH_PROGS],[])
AC_DEFUN([BITCOIN_QT_CONFIGURE],[])
AC_DEFUN([_BITCOIN_QT_CHECK_QT5],[])
AC_DEFUN([_BITCOIN_QT_IS_STATIC],[])
AC_DEFUN([_BITCOIN_QT_CHECK_STATIC_PLUGINS],[])
AC_DEFUN([_BITCOIN_QT_FIND_LIBS_WITH_PKGCONFIG],[])
AC_DEFUN([_BITCOIN_QT_FIND_LIBS_WITHOUT],[])