// Copyright (c) 2011-2014 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "splashscreen.h"

#include "clientversion.h"
#include "init.h"
#include "ui_interface.h"
#include "util.h"
#ifdef ENABLE_WALLET
#include "wallet.h"
#endif

#include <boost/bind/bind.hpp>
using namespace boost::placeholders;

#include <QApplication>
#include <QPainter>

SplashScreen::SplashScreen(Qt::WindowFlags f, bool isTestNet) :
    QSplashScreen(QPixmap(isTestNet ? ":/images/splash_testnet" : ":/images/splash"), f),
    curAlignment(0)
{
    // set reference point, paddings
    int paddingRight            = 50;
    int paddingTop              = 60;
    int titleVersionVSpace      = 25;
    int titleCopyrightVSpace    = 40;
    int titleCopyright2VSpace   = 15;

    float fontFactor            = 1.1;

    // define text to place
    QString titleText       = tr("Cryptonite");
    QString versionText     = QString("Version %1").arg(QString::fromStdString(FormatFullVersion()));
    QString copyrightText   = QChar(0xA9)+QString(" 2009-%1 ").arg(COPYRIGHT_YEAR) + QString(tr("The Bitcoin Core developers"));
    QString copyright2Text  = QChar(0xA9)+QString(" 2014 ") + QString(tr("The Mini-Blockchain Project"));
    QString testnetAddText  = QString(tr("[testnet]"));

    QString font            = "Open Sans";

    // Paint title, version and copyright onto the splash bitmap. We take
    // a mutable copy of the pixmap so subsequent QSplashScreen::showMessage()
    // calls (which render via Qt's own painter on top of this pixmap) line up
    // with the baked-in layout.
    QPixmap pixmap = this->pixmap();
    QPainter pixPaint(&pixmap);
    pixPaint.setPen(QColor(Qt::lightGray));

    // check font size and drawing with
    pixPaint.setFont(QFont(font, 33*fontFactor));
    QFontMetrics fm = pixPaint.fontMetrics();
    int titleTextWidth  = fm.horizontalAdvance(titleText);
    if(titleTextWidth > 160) {
        // strange font rendering, Arial probably not found
        fontFactor = 0.75;
    }

    pixPaint.setFont(QFont(font, 33*fontFactor));
    fm = pixPaint.fontMetrics();
    titleTextWidth  = fm.horizontalAdvance(titleText);
    pixPaint.drawText(pixmap.width()-titleTextWidth-paddingRight,paddingTop,titleText);

    pixPaint.setFont(QFont(font, 15*fontFactor));

    // if the version string is to long, reduce size
    fm = pixPaint.fontMetrics();
    int versionTextWidth  = fm.horizontalAdvance(versionText);
    if(versionTextWidth > titleTextWidth+paddingRight-10) {
        pixPaint.setFont(QFont(font, 10*fontFactor));
        titleVersionVSpace -= 5;
    }
    pixPaint.drawText(pixmap.width()-titleTextWidth-paddingRight+2,paddingTop+titleVersionVSpace,versionText);

    // draw copyright stuff
    pixPaint.setFont(QFont(font, 10*fontFactor));
    pixPaint.drawText(pixmap.width()-titleTextWidth-paddingRight,paddingTop+titleCopyrightVSpace,copyrightText);
    pixPaint.drawText(pixmap.width()-titleTextWidth-paddingRight,paddingTop+titleCopyrightVSpace+titleCopyright2VSpace,copyright2Text);

    // draw testnet string if testnet is on
    if(isTestNet) {
        QFont boldFont = QFont(font, 10*fontFactor);
        boldFont.setWeight(QFont::Bold);
        pixPaint.setFont(boldFont);
        fm = pixPaint.fontMetrics();
        int testnetAddTextWidth  = fm.horizontalAdvance(testnetAddText);
        pixPaint.drawText(pixmap.width()-testnetAddTextWidth-10,15,testnetAddText);
    }

    pixPaint.end();

    // Swap in the baked pixmap so QSplashScreen's own rendering uses it.
    setPixmap(pixmap);

    // Set window title (also gives the splash a title bar entry on the WM).
    if(isTestNet)
        setWindowTitle(titleText + " " + testnetAddText);
    else
        setWindowTitle(titleText);

    subscribeToCoreSignals();
}

SplashScreen::~SplashScreen()
{
    unsubscribeFromCoreSignals();
}

static void InitMessage(SplashScreen *splash, const std::string &message)
{
    QMetaObject::invokeMethod(splash, "showMessage",
        Qt::QueuedConnection,
        Q_ARG(QString, QString::fromStdString(message)),
        Q_ARG(int, Qt::AlignBottom|Qt::AlignHCenter),
        Q_ARG(QColor, QColor(Qt::white)));
}

static void ShowProgressF(SplashScreen *splash, const std::string &title, int nProgress)
{
    InitMessage(splash, title + strprintf("%d", nProgress) + "%");
}

#ifdef ENABLE_WALLET
static void ConnectWallet(SplashScreen *splash, CWallet* wallet)
{
    ShowProgress.connect(boost::bind(ShowProgressF, splash, _1, _2));
}
#endif

void SplashScreen::subscribeToCoreSignals()
{
    // Connect signals to client
    uiInterface.InitMessage.connect(boost::bind(InitMessage, this, _1));
#ifdef ENABLE_WALLET
    uiInterface.LoadWallet.connect(boost::bind(ConnectWallet, this, _1));
#endif
}

void SplashScreen::unsubscribeFromCoreSignals()
{
    // Disconnect signals from client.
    // Note: ShowProgress is connected inside ConnectWallet() on a per-wallet
    // basis. Disconnecting it here with a freshly-bound functor would be a
    // bind/connect mismatch that silently no-ops, so we leave wallet-scoped
    // ShowProgress connections to the wallet itself. The splash is destroyed
    // before any wallet operation that would invoke ShowProgress, so the
    // connections become unreachable and are cleaned up when the wallet dies.
    uiInterface.InitMessage.disconnect(boost::bind(InitMessage, this, _1));
}

void SplashScreen::showMessage(const QString &message, int alignment, const QColor &color)
{
    curMessage = message;
    curAlignment = alignment;
    curColor = color;
    QSplashScreen::showMessage(message, alignment, color);
}