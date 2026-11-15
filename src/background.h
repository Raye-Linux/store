// SPDX-License-Identifier: GPL-3.0-or-later
// Installs keep going when the window closes, like on Windows: the
// window hides, the Store stays until the last job of the queue is over,
// then it quits. Opening the Store again brings the same window back.
#pragma once

#include <QObject>
#include <QStringList>

class Installer;

class Background : public QObject
{
    Q_OBJECT
    // the window is closed but the Store still works
    Q_PROPERTY(bool hidden READ hidden NOTIFY hiddenChanged)
public:
    explicit Background(Installer *installer, QObject *parent = nullptr);

    bool hidden() const { return m_hidden; }
    // how long the Store waits after the last job before it quits, so
    // the "is ready" notification has gone out
    void setQuitDelay(int ms) { m_quitDelay = ms; }

    // the window was asked to close: true when the Store keeps working
    // (the window only hides), false when it quits now
    Q_INVOKABLE bool windowClosing();
    // the window is on screen again
    Q_INVOKABLE void windowShown();

Q_SIGNALS:
    void hiddenChanged();
    // the window closed while these apps were still being worked on
    void keptRunning(const QStringList &names);
    void quitRequested();

private:
    void checkIdle();
    QStringList workingNames() const;

    Installer *m_installer;
    bool m_hidden = false;
    bool m_quitting = false;
    int m_quitDelay = 3000;
};
