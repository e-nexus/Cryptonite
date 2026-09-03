# ============================================================================
# bitcoin_qt.m4 — stubbed out for the Cryptonite Sept-2026 build.
# ============================================================================
#
# The Qt4/Qt5 GUI support in this codebase targets Qt4 APIs that are no longer
# supported on modern toolchains. Until src/qt/ is ported to a current Qt
# version (Qt5 or Qt6), the GUI build is intentionally disabled. See
# configure.ac for the corresponding bitcoin_enable_qt=no guard.
#
# All AC_DEFUN macros below are kept as no-ops so that any leftover
# m4_include references in configure.ac do not break the build. The macros
# intentionally do nothing — the configure script never invokes them.

AC_DEFUN([BITCOIN_QT_FAIL],[])
AC_DEFUN([BITCOIN_QT_CHECK],[])
AC_DEFUN([BITCOIN_QT_PATH_PROGS],[])
AC_DEFUN([BITCOIN_QT_INIT],[])
AC_DEFUN([BITCOIN_QT_CONFIGURE],[])
AC_DEFUN([_BITCOIN_QT_CHECK_QT5],[])
AC_DEFUN([_BITCOIN_QT_IS_STATIC],[])
AC_DEFUN([_BITCOIN_QT_CHECK_STATIC_PLUGINS],[])
AC_DEFUN([_BITCOIN_QT_FIND_LIBS_WITH_PKGCONFIG],[])
AC_DEFUN([_BITCOIN_QT_FIND_LIBS_WITHOUT_PKGCONFIG],[])