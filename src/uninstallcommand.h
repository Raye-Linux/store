// SPDX-License-Identifier: GPL-3.0-or-later
// raye-store --uninstall <app id|ref>: removes one app with no window and
// tells whoever started it how it goes, one JSON object per line. Settings
// > Apps > Installed apps reads this.
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>

class Installer;
class QIODevice;

struct UninstallTarget {
    QString source; // "flatpak" or "apt"; empty when there is nothing to remove
    QString ref;
    QString appId;
};

// What a Settings row names, as a job can take it: a flatpak ref as it is,
// an id through what is installed. `flatpakRefs` lists installed apps as
// "app/<id>/<arch>/<branch>"; `packageOf` gives the package that owns a
// launcher, or nothing.
UninstallTarget resolveUninstallTarget(const QString &argument, const QStringList &flatpakRefs,
                                       const std::function<QString(const QString &desktopFile)> &packageOf);
// The same, looking at this PC.
UninstallTarget resolveUninstallTarget(const QString &argument);

class UninstallCommand : public QObject
{
    Q_OBJECT
public:
    UninstallCommand(Installer *installer, QIODevice *out, QObject *parent = nullptr);
    // answers with done(); `name` is what the messages call the app
    void start(const UninstallTarget &target);

Q_SIGNALS:
    void done(int exitCode);

private:
    void say(const QString &state, int percent, const QString &message);

    Installer *m_installer;
    QIODevice *m_out;
    QString m_ref;
    int m_percent = 0;
};
