// SPDX-License-Identifier: GPL-3.0-or-later
#include "background.h"

#include "installer.h"

#include <QTimer>

Background::Background(Installer *installer, QObject *parent)
    : QObject(parent)
    , m_installer(installer)
{
    // the queue empties before the finished signals go out: look again
    // once they have
    connect(installer, &Installer::busyChanged, this, [this] {
        QTimer::singleShot(0, this, &Background::checkIdle);
    });
}

bool Background::windowClosing()
{
    if (!m_installer->busy()) {
        m_quitting = true;
        Q_EMIT quitRequested();
        return false;
    }
    if (!m_hidden) {
        m_hidden = true;
        Q_EMIT hiddenChanged();
    }
    Q_EMIT keptRunning(workingNames());
    return true;
}

void Background::windowShown()
{
    if (!m_hidden)
        return;
    m_hidden = false;
    Q_EMIT hiddenChanged();
}

void Background::checkIdle()
{
    if (!m_hidden || m_quitting || m_installer->busy())
        return;
    m_quitting = true;
    QTimer::singleShot(m_quitDelay, this, [this] {
        // shown again, or a new job came in the meantime
        m_quitting = false;
        if (m_hidden && !m_installer->busy()) {
            m_quitting = true;
            Q_EMIT quitRequested();
        }
    });
}

QStringList Background::workingNames() const
{
    QStringList names;
    const auto *jobs = qobject_cast<const JobsModel *>(m_installer->jobs());
    if (!jobs)
        return names;
    for (const JobsModel::Row &row : jobs->rows()) {
        if (!names.contains(row.spec.name))
            names.append(row.spec.name);
    }
    return names;
}
