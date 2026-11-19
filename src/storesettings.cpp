// SPDX-License-Identifier: GPL-3.0-or-later
#include "storesettings.h"
#include "softwaresources.h"

#include <QProcess>
#include <QStandardPaths>

namespace
{
const QString autoUpdateKey = QStringLiteral("Updates/Automatic");
const QString systemPackagesKey = QStringLiteral("Search/ShowSystemPackages");
}

StoreSettings::StoreSettings(QObject *parent)
    : StoreSettings(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/raye-storerc"), parent)
{
}

StoreSettings::StoreSettings(const QString &path, QObject *parent)
    : QObject(parent)
    , m_file(path, QSettings::IniFormat)
    , m_helper({QStringLiteral("pkexec"), QStringLiteral("/usr/libexec/raye-store-extra")})
{
}

// on unless turned off: what the timer assumes too
bool StoreSettings::autoUpdate() const { return m_file.value(autoUpdateKey, true).toBool(); }
void StoreSettings::setAutoUpdate(bool on) { set(autoUpdateKey, on); }
bool StoreSettings::showSystemPackages() const { return m_file.value(systemPackagesKey, false).toBool(); }
void StoreSettings::setShowSystemPackages(bool on) { set(systemPackagesKey, on); }

void StoreSettings::set(const QString &key, bool value)
{
    if (m_file.value(key).isValid() && m_file.value(key).toBool() == value)
        return;
    m_file.setValue(key, value);
    m_file.sync();
    Q_EMIT changed();
}

void StoreSettings::readSources()
{
    m_sources = SoftwareSources::read();
    Q_EMIT sourcesChanged();
}

void StoreSettings::repair()
{
    runHelper(QStringLiteral("repair"), QStringLiteral("repairing"), QStringLiteral("repaired"), QStringLiteral("repairFailed"));
}

void StoreSettings::cleanDownloads()
{
    runHelper(QStringLiteral("clean"), QStringLiteral("cleaning"), QStringLiteral("cleaned"), QStringLiteral("cleanFailed"));
}

void StoreSettings::openRayeUpdate()
{
    QProcess::startDetached(QStringLiteral("raye-settings"), {QStringLiteral("update")});
}

void StoreSettings::runHelper(const QString &verb, const QString &busy, const QString &done, const QString &failed)
{
    if (m_maintenance == QLatin1String("repairing") || m_maintenance == QLatin1String("cleaning"))
        return;
    auto *process = new QProcess(this);
    process->setProgram(m_helper.value(0));
    process->setArguments(m_helper.mid(1) << verb);
    process->setProcessChannelMode(QProcess::ForwardedErrorChannel);
    connect(process, &QProcess::finished, this, [this, process, done, failed](int code, QProcess::ExitStatus status) {
        setMaintenance(status == QProcess::NormalExit && code == 0 ? done : failed);
        process->deleteLater();
    });
    connect(process, &QProcess::errorOccurred, this, [this, process, failed](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            setMaintenance(failed);
            process->deleteLater();
        }
    });
    setMaintenance(busy);
    process->start();
}

void StoreSettings::setMaintenance(const QString &state)
{
    if (m_maintenance == state)
        return;
    m_maintenance = state;
    Q_EMIT maintenanceChanged();
}
