// SPDX-License-Identifier: GPL-3.0-or-later
#include "storesettings.h"

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
