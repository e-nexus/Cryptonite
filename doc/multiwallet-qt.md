Multiwallet Qt Development and Integration Strategy
===================================================

**Note: the Qt GUI is not currently supported in this fork.** This
document is preserved for historical context (it describes the original
multiwallet architecture as designed in Bitcoin Core 0.10) but the
code paths it references are not built. To re-enable the Qt GUI in the
future, src/qt/ would need to be ported from Qt4 to Qt5 and re-included
in DIST_SUBDIRS / AC_CONFIG_FILES.

---

In order to support loading of multiple wallets in cryptonited (the
GUI was Bitcoin-Qt in the original Bitcoin Core codebase), a few
changes in the UI architecture will be needed.
Fortunately, only four of the files in the existing project are affected by this change.

Two new classes have been implemented in two new .h/.cpp file pairs, with much of the functionality that was previously
implemented in the BitcoinGUI class moved over to these new classes.

The two existing files most affected, by far, are bitcoingui.h and bitcoingui.cpp, as the BitcoinGUI class will require
some major rewrites. It is expected that this rewrite will be fairly large and will require careful review.

The remaining two files are the entry point bitcoin.cpp and the wallet model walletmodel.cpp, which will only require
small modifications.

Two new headers and source files will have to be added to bitcoin-qt.pro.

In multiprocess mode, a new component bitcoingui.cpp will exist for handling the GUI; in singleprocess mode this
class will not exist at all, instead being replaced by a stub class that is a no-op.

[Document archived for reference; not under active maintenance.]