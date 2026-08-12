#ifndef MAINWINDOWLAYOUT_H
#define MAINWINDOWLAYOUT_H

#include <QByteArray>

class QDockWidget;
class QMainWindow;

void RestoreMainWindowStateWithRequiredDevicesDock(
    QMainWindow *window, QDockWidget *devicesDock, const QByteArray &state);

#endif
