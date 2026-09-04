// Copyright (c) 2011-2014 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef SPLASHSCREEN_H
#define SPLASHSCREEN_H

#include <QSplashScreen>

/**
 * Thin wrapper around QSplashScreen that paints the version / copyright text
 * onto the splash bitmap and forwards core progress messages to it.
 *
 * QSplashScreen's stock behavior (moveable, has a close button, deletes on
 * finish()) already covers everything the legacy hand-rolled SplashScreen
 * class got wrong; this subclass only exists so we can render the title,
 * version, copyright and optional [testnet] marker on top of the PNG.
 */
class SplashScreen : public QSplashScreen
{
    Q_OBJECT

public:
    explicit SplashScreen(Qt::WindowFlags f, bool isTestNet);
    ~SplashScreen();

public Q_SLOTS:
    /** Show message and progress */
    void showMessage(const QString &message, int alignment, const QColor &color);

private:
    /** Connect core signals to splash screen */
    void subscribeToCoreSignals();
    /** Disconnect core signals from splash screen */
    void unsubscribeFromCoreSignals();

    QString curMessage;
    QColor curColor;
    int curAlignment;
};

#endif // SPLASHSCREEN_H