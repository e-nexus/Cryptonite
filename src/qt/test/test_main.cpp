#include "cryptonite-config.h"
#if defined(HAVE_CONFIG_H)
#include "cryptonite-config.h"
#endif

#include "uritests.h"

#include <QCoreApplication>
#include <QObject>
#include <QTest>

// This is all you need to run all the tests
int main(int argc, char *argv[])
{
    bool fInvalid = false;

    // Don't remove this, it's needed to access
    // QCoreApplication:: in the tests
    QCoreApplication app(argc, argv);
    app.setApplicationName("Cryptonite-Qt-test");

    URITests test1;
    if (QTest::qExec(&test1) != 0)
        fInvalid = true;

    return fInvalid;
}