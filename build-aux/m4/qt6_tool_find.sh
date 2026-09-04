# Sourced by configure after BITCOIN_QT_INIT discovers Qt 6 modules. Lives
# in a separate file so autoconf's m4 layer cannot strip the shell function's
# positional parameters ($1, $2).
#
# QT_LIBEXECDIR and QT_BINDIR must already be set in the calling shell.
#
# Usage: pick_qt6_tool <qt6-binary-name> <legacy-name>
# Example: pick_qt6_tool moc6 moc
#
# Prints the first executable whose real path lives under /usr/lib/qt6/,
# or prints "no" if none of the candidates qualify.
pick_qt6_tool () {
  for candidate in \
    "$QT_LIBEXECDIR/$1" \
    "$QT_LIBEXECDIR/$2" \
    "$QT_BINDIR/$1" \
    "$QT_BINDIR/$2" \
    "/usr/lib/qt6/libexec/$1" \
    "/usr/lib/qt6/libexec/$2" \
    "/usr/lib/qt6/bin/$1" \
    "/usr/lib/qt6/bin/$2" \
    "$1" "$2"; do
    if test -x "$candidate"; then
      real=`readlink -f "$candidate" 2>/dev/null`
      case "$real" in
        /usr/lib/qt6/*)
          echo "$candidate"
          return 0
          ;;
      esac
    fi
  done
  echo no
  return 1
}
