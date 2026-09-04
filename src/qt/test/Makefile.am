include $(top_srcdir)/src/Makefile.include

AM_CPPFLAGS += -I$(top_srcdir)/src -std=c++17 -D__STDC_LIMIT_MACROS \
  -I$(top_srcdir)/src/qt \
  -I$(top_builddir)/src/qt \
  $(QR_CFLAGS)
bin_PROGRAMS = test_cryptonite-qt
TESTS = test_cryptonite-qt

TEST_QT_MOC_CPP = moc_uritests.cpp

TEST_QT_H = \
  uritests.h

BUILT_SOURCES = $(TEST_QT_MOC_CPP)

test_cryptonite_qt_CPPFLAGS = $(AM_CPPFLAGS) $(QT_INCLUDES) $(QT_TEST_INCLUDES)

test_cryptonite_qt_SOURCES = \
  test_main.cpp \
  uritests.cpp \
  $(TEST_QT_H)

nodist_test_cryptonite_qt_SOURCES = $(TEST_QT_MOC_CPP)

test_cryptonite_qt_LDADD = $(LIBBITCOINQT) $(LIBBITCOIN_SERVER)
if ENABLE_WALLET
test_cryptonite_qt_LDADD += $(LIBBITCOIN_WALLET)
endif
test_cryptonite_qt_LDADD += $(LIBBITCOIN_CLI) $(LIBBITCOIN_COMMON) $(LIBLEVELDB) \
  $(LIBMEMENV) $(BOOST_LIBS) $(QT_DBUS_LIBS) $(QT_TEST_LIBS) $(QT_LIBS) \
  $(QR_LIBS) $(BDB_LIBS)
test_cryptonite_qt_LDFLAGS = $(QT_LDFLAGS)

CLEANFILES = $(BUILT_SOURCES) *.gcda *.gcno