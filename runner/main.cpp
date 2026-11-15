// SPDX-License-Identifier: GPL-3.0-or-later
// raye-store-runner: answers the Start menu's search with apps to get
// from the Store, "Get Discord…" under "Raye Store" (org.kde.krunner1 on
// the session bus, name org.raye.store.runner, path /runner). Started by
// D-Bus on the first search; quits after two quiet minutes.
//
// It keeps only what the search reads (searchEntries), and keeps that on
// disk too: a new start reads the small index instead of AppStream, and
// AppStream is read again only when one of its sources changed.

#include "catalog.h"
#include "runnermatch.h"

#include <KLocalizedString>
#include <QCoreApplication>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusContext>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QElapsedTimer>
#include <QHash>
#include <QImage>
#include <QLocale>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>

#include <algorithm>
#include <utility>

#ifdef __GLIBC__
#include <malloc.h>
#endif

namespace
{

constexpr int idleQuitMs = 2 * 60 * 1000;
constexpr int installedFreshMs = 20 * 1000;
constexpr int iconSize = 64;
// icons already turned into pixels; a long session does not grow past this
constexpr int maxCachedIcons = 48;

QString indexFile()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + QStringLiteral("/raye-store/search-index");
}

// AppStream gives names in the user's language: another language, another index
QString language()
{
    return QLocale().uiLanguages().join(QLatin1Char(':'));
}

// what reading AppStream freed goes back to the system, not to the heap
void giveBackFreedMemory()
{
#ifdef __GLIBC__
    malloc_trim(0);
#endif
}

struct RemoteAction {
    QString id;
    QString text;
    QString iconName;
};

struct RemoteMatch {
    QString id;
    QString text;
    QString iconName;
    int categoryRelevance = 0;
    double relevance = 0;
    QVariantMap properties;
};

// an icon as pixels, the way KRunner takes them ("icon-data")
struct RemoteImage {
    int width = 0;
    int height = 0;
    int rowStride = 0;
    bool hasAlpha = true;
    int bitsPerSample = 8;
    int channels = 4;
    QByteArray data;
};

} // namespace

typedef QList<RemoteAction> RemoteActions;
typedef QList<RemoteMatch> RemoteMatches;
Q_DECLARE_METATYPE(RemoteAction)
Q_DECLARE_METATYPE(RemoteActions)
Q_DECLARE_METATYPE(RemoteMatch)
Q_DECLARE_METATYPE(RemoteMatches)
Q_DECLARE_METATYPE(RemoteImage)

QDBusArgument &operator<<(QDBusArgument &argument, const RemoteAction &action)
{
    argument.beginStructure();
    argument << action.id << action.text << action.iconName;
    argument.endStructure();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, RemoteAction &action)
{
    argument.beginStructure();
    argument >> action.id >> action.text >> action.iconName;
    argument.endStructure();
    return argument;
}

QDBusArgument &operator<<(QDBusArgument &argument, const RemoteMatch &match)
{
    argument.beginStructure();
    argument << match.id << match.text << match.iconName << match.categoryRelevance << match.relevance << match.properties;
    argument.endStructure();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, RemoteMatch &match)
{
    argument.beginStructure();
    argument >> match.id >> match.text >> match.iconName >> match.categoryRelevance >> match.relevance >> match.properties;
    argument.endStructure();
    return argument;
}

QDBusArgument &operator<<(QDBusArgument &argument, const RemoteImage &image)
{
    argument.beginStructure();
    argument << image.width << image.height << image.rowStride << image.hasAlpha << image.bitsPerSample << image.channels
             << image.data;
    argument.endStructure();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, RemoteImage &image)
{
    argument.beginStructure();
    argument >> image.width >> image.height >> image.rowStride >> image.hasAlpha >> image.bitsPerSample >> image.channels
        >> image.data;
    argument.endStructure();
    return argument;
}

class Runner : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.krunner1")

public:
    explicit Runner(QObject *parent = nullptr)
        : QObject(parent)
    {
        m_idle.setSingleShot(true);
        m_idle.setInterval(idleQuitMs);
        connect(&m_idle, &QTimer::timeout, qApp, &QCoreApplication::quit);
        m_idle.start();

        const QString stamp = sourceStamp(defaultSourceRoots(), language());
        if (auto entries = readSearchIndex(indexFile(), stamp)) {
            m_apps = std::move(*entries);
            m_loaded = true;
            return;
        }
        // first start, or the catalog changed: read it all once, keep the little
        auto *catalog = new Catalog(this);
        connect(catalog, &Catalog::loadingChanged, this, [this, catalog, stamp] {
            if (catalog->loading())
                return;
            QList<App> entries = searchEntries(catalog->apps());
            if (!writeSearchIndex(indexFile(), stamp, entries))
                qWarning("raye-store-runner: cannot write %s", qPrintable(indexFile()));
            // read back what was written: the strings then live in fresh memory of
            // their own, not scattered over the pages AppStream used, which can then
            // all go back to the system
            m_apps = readSearchIndex(indexFile(), stamp).value_or(entries);
            entries.clear();
            m_loaded = true;
            // once the catalog and what it owns (the loaded copy too) are gone
            connect(catalog, &QObject::destroyed, this, [] { QTimer::singleShot(0, &giveBackFreedMemory); });
            catalog->deleteLater();
            // the searches that came while the catalog loaded
            const auto waiting = std::exchange(m_waiting, {});
            for (const auto &[message, query] : waiting)
                QDBusConnection::sessionBus().send(message.createReply(QVariant::fromValue(matches(query))));
        });
        catalog->reload();
    }

public Q_SLOTS:
    RemoteActions Actions()
    {
        m_idle.start();
        return {};
    }

    QVariantMap Config()
    {
        m_idle.start();
        return {};
    }

    RemoteMatches Match(const QString &query)
    {
        m_idle.start();
        if (!m_loaded) {
            setDelayedReply(true);
            m_waiting.append({message(), query});
            return {};
        }
        return matches(query);
    }

    void Run(const QString &matchId, const QString &actionId)
    {
        Q_UNUSED(actionId)
        m_idle.start();
        // only ids of the catalog are opened, never what came as it came
        const auto known = std::find_if(m_apps.cbegin(), m_apps.cend(), [&matchId](const App &a) { return a.id == matchId; });
        if (known == m_apps.cend())
            return;
        QProcess::startDetached(QStringLiteral("raye-store"), storeLinkArguments(known->id));
    }

    void Teardown()
    {
        m_idle.start();
    }

private:
    RemoteMatches matches(const QString &query)
    {
        // apps installed since the last look are not offered again
        if (!m_installedAge.isValid() || m_installedAge.elapsed() > installedFreshMs) {
            m_installed.refreshInstalled();
            m_installedAge.start();
        }
        RemoteMatches out;
        const auto installed = [this](const QString &ref) { return m_installed.isInstalled(ref); };
        for (const StoreMatch &m : storeMatches(m_apps, installed, query)) {
            RemoteMatch r;
            r.id = m.id;
            r.text = m.text;
            r.categoryRelevance = 30; // after the apps already installed
            r.relevance = m.relevance;
            r.properties.insert(QStringLiteral("subtext"), m.subtext);
            if (m.icon.startsWith(QLatin1Char('/'))) {
                const RemoteImage image = pixels(m.icon);
                if (image.width > 0)
                    r.properties.insert(QStringLiteral("icon-data"), QVariant::fromValue(image));
                else
                    r.iconName = QStringLiteral("system-software-install");
            } else {
                r.iconName = m.icon;
            }
            out.append(r);
        }
        return out;
    }

    RemoteImage pixels(const QString &file)
    {
        auto it = m_icons.constFind(file);
        if (it != m_icons.cend())
            return *it;
        RemoteImage image;
        QImage img(file);
        if (!img.isNull()) {
            img = img.scaled(iconSize, iconSize, Qt::KeepAspectRatio, Qt::SmoothTransformation)
                      .convertToFormat(QImage::Format_RGBA8888);
            image.width = img.width();
            image.height = img.height();
            image.rowStride = int(img.bytesPerLine());
            image.data = QByteArray(reinterpret_cast<const char *>(img.constBits()), img.sizeInBytes());
        }
        if (m_icons.size() >= maxCachedIcons)
            m_icons.clear();
        m_icons.insert(file, image);
        return image;
    }

    QList<App> m_apps;  // searchEntries only
    Catalog m_installed; // never loads apps: only says what is installed
    bool m_loaded = false;
    QElapsedTimer m_installedAge;
    QList<std::pair<QDBusMessage, QString>> m_waiting;
    QHash<QString, RemoteImage> m_icons;
    QTimer m_idle;
};

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("raye-store-runner"));
    KLocalizedString::setApplicationDomain("raye-store");

    qDBusRegisterMetaType<RemoteAction>();
    qDBusRegisterMetaType<RemoteActions>();
    qDBusRegisterMetaType<RemoteMatch>();
    qDBusRegisterMetaType<RemoteMatches>();
    qDBusRegisterMetaType<RemoteImage>();

    Runner runner;
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.registerObject(QStringLiteral("/runner"), &runner, QDBusConnection::ExportAllSlots)) {
        qCritical("raye-store-runner: cannot put /runner on the session bus");
        return 1;
    }
    if (!bus.registerService(QStringLiteral("org.raye.store.runner"))) {
        qCritical("raye-store-runner: org.raye.store.runner is taken");
        return 1;
    }
    return app.exec();
}

#include "main.moc"
