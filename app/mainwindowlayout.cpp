#include "mainwindowlayout.h"

#include <QDockWidget>
#include <QMainWindow>

void RestoreMainWindowStateWithRequiredDevicesDock(
    QMainWindow *window, QDockWidget *devicesDock, const QByteArray &state)
{
    if (!state.isEmpty())
        window->restoreState(state);

    // The Devices pane is primary navigation. Older state, or state written by
    // an earlier preview build, may restore it hidden or floating.
    devicesDock->setFloating(false);
    window->addDockWidget(Qt::LeftDockWidgetArea, devicesDock);
    devicesDock->show();
}
