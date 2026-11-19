// SPDX-License-Identifier: GPL-3.0-or-later
// The Store's settings page. They live in raye-storerc, where the daily
// update timer reads them with kreadconfig6.
#pragma once

#include <QObject>
#include <QSettings>
#include <QStringList>
#include <QVariantList>

class StoreSettings : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool autoUpdate READ autoUpdate WRITE setAutoUpdate NOTIFY changed)
    Q_PROPERTY(bool showSystemPackages READ showSystemPackages WRITE setShowSystemPackages NOTIFY changed)
    // where apps come from (SoftwareSources), read when the page asks
    Q_PROPERTY(QVariantList sources READ sources NOTIFY sourcesChanged)
    // "", "repairing", "repaired", "repairFailed", "cleaning", "cleaned", "cleanFailed"
    Q_PROPERTY(QString maintenance READ maintenance NOTIFY maintenanceChanged)
public:
    explicit StoreSettings(QObject *parent = nullptr);
    // the file itself, for the tests
    explicit StoreSettings(const QString &path, QObject *parent = nullptr);

    bool autoUpdate() const;
    void setAutoUpdate(bool on);
    bool showSystemPackages() const;
    void setShowSystemPackages(bool on);

    QVariantList sources() const { return m_sources; }
    Q_INVOKABLE void readSources();
    QString maintenance() const { return m_maintenance; }
    // finish half-done installs, let apt fix what is missing, drop partial
    // downloads (raye-store-extra repair, with the password)
    Q_INVOKABLE void repair();
    // empty apt's download cache (raye-store-extra clean)
    Q_INVOKABLE void cleanDownloads();
    // System updates are Raye Update's, in Settings
    Q_INVOKABLE void openRayeUpdate();
    // the command the verb is added to: pkexec and the helper, but in the tests
    void setHelper(const QStringList &helper) { m_helper = helper; }

Q_SIGNALS:
    void changed();
    void sourcesChanged();
    void maintenanceChanged();

private:
    void set(const QString &key, bool value);
    void runHelper(const QString &verb, const QString &busy, const QString &done, const QString &failed);
    void setMaintenance(const QString &state);

    QSettings m_file;
    QVariantList m_sources;
    QString m_maintenance;
    QStringList m_helper;
};
