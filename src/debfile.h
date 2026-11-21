// SPDX-License-Identifier: GPL-3.0-or-later
// A .deb file someone opened, for the small install window: what is in
// it (raye-store-extra info, as the person), then installing it through
// apt (raye-store-extra install-file, as root through pkexec, which asks
// for the password).
#pragma once

#include <QJsonObject>
#include <QLocale>
#include <QObject>
#include <QProcess>
#include <QStringList>
#include <QTemporaryDir>
#include <QUrl>

class DebFile : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int state READ state NOTIFY changed)
    Q_PROPERTY(int relation READ relation NOTIFY changed)
    Q_PROPERTY(QString fileName READ fileName CONSTANT)
    Q_PROPERTY(QString name READ name NOTIFY changed)
    Q_PROPERTY(QString packageName READ packageName NOTIFY changed)
    Q_PROPERTY(QString version READ version NOTIFY changed)
    Q_PROPERTY(QString installedVersion READ installedVersion NOTIFY changed)
    Q_PROPERTY(QString publisher READ publisher NOTIFY changed)
    Q_PROPERTY(QString summary READ summary NOTIFY changed)
    Q_PROPERTY(QUrl icon READ icon NOTIFY changed)
    Q_PROPERTY(QString sizeText READ sizeText NOTIFY changed)
    Q_PROPERTY(int percent READ percent NOTIFY changed)
    Q_PROPERTY(QString phaseText READ phaseText NOTIFY changed)
    Q_PROPERTY(QString errorTitle READ errorTitle NOTIFY changed)
    Q_PROPERTY(QString errorText READ errorText NOTIFY changed)
    Q_PROPERTY(QString details READ details NOTIFY changed)
    Q_PROPERTY(bool canOpen READ canOpen NOTIFY changed)
    // packages installing it removes because it takes their place
    Q_PROPERTY(QStringList replaces READ replaces NOTIFY changed)
    // the Store's own copy of the same app, when it has one it advises
    Q_PROPERTY(QString storeAppId READ storeAppId NOTIFY changed)
    Q_PROPERTY(QString replacesText READ replacesText NOTIFY changed)
public:
    enum State { Reading, Ready, Installing, Done, Failed };
    Q_ENUM(State)
    // the same package on this PC, compared with the file
    enum Relation { NotInstalled, UpdateAvailable, AlreadyInstalled, NewerInstalled };
    Q_ENUM(Relation)

    // helper: the command before "info"; privileged: the one before
    // "install-file". The tests put their own.
    explicit DebFile(const QString &path, const QStringList &helper = {}, const QStringList &privileged = {},
                     QObject *parent = nullptr);

    int state() const { return m_state; }
    int relation() const { return m_relation; }
    QString fileName() const;
    QString path() const { return m_path; }
    QString name() const { return m_name; }
    QString packageName() const { return m_package; }
    QString version() const { return m_version; }
    QString installedVersion() const { return m_installed; }
    QString publisher() const { return m_publisher; }
    QString summary() const { return m_summary; }
    QUrl icon() const { return m_icon; }
    QString sizeText() const { return m_sizeText; }
    int percent() const { return m_percent; }
    QString phaseText() const { return m_phaseText; }
    QString errorTitle() const { return m_errorTitle; }
    QString errorText() const { return m_errorText; }
    QString details() const { return m_details; }
    bool canOpen() const { return !m_launcher.isEmpty(); }
    QString launcher() const { return m_launcher; }
    QStringList replaces() const { return m_replaces; }
    QString storeAppId() const { return m_storeAppId; }

    // reads the file; Ready or Failed follows
    Q_INVOKABLE void load();
    Q_INVOKABLE void install();
    // starts the app it installed, the way the Start menu does
    Q_INVOKABLE bool open();
    // opens the Store on storeAppId's page
    Q_INVOKABLE bool openInStore();
    // "This replaces steam-devices." in plain words, or nothing
    QString replacesText() const;
    // the Store app the Store advises over a .deb of this package
    static QString storeAppFor(const QString &package);

    // plain words for a reason the helper gave; what is the package it
    // is about, or the processor type for "architecture"
    static QString reasonText(const QString &code, const QString &what = {});
    // the app's name in the language of locale, from {"": "...", "fr": "..."}
    static QString pickName(const QJsonObject &names, const QLocale &locale = QLocale());
    // "Jane Maker <jane@example.org>" -> "Jane Maker"
    static QString publisherName(const QString &maintainer);

Q_SIGNALS:
    void changed();

private:
    void readInfo(int code);
    void readLines();
    void installEnded(int code, QProcess::ExitStatus status);
    void fail(const QString &title, const QString &text);

    QString m_path;
    QStringList m_helper;
    QStringList m_privileged;
    QTemporaryDir m_icons;
    QProcess m_reader;
    QProcess m_installer;
    QByteArray m_buffer;
    QString m_reason;
    QString m_reasonWhat;
    QString m_sha256;
    QString m_stderr;

    int m_state = Reading;
    int m_relation = NotInstalled;
    QString m_name;
    QString m_package;
    QString m_version;
    QString m_installed;
    QString m_publisher;
    QString m_summary;
    QUrl m_icon;
    QString m_sizeText;
    int m_percent = -1;
    QString m_phaseText;
    QString m_errorTitle;
    QString m_errorText;
    QString m_details;
    QString m_launcher;
    QStringList m_replaces;
    QString m_storeAppId;
    QStringList m_storeCommand{QStringLiteral("raye-store"), QStringLiteral("--open")};

public:
    // the tests put their own command before the app id
    void setStoreCommand(const QStringList &command) { m_storeCommand = command; }
};
