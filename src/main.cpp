// SPDX-License-Identifier: GPL-3.0-or-later
// Raye Store: the apps for Raye Linux in one place.

#include "applink.h"
#include "background.h"
#include "catalog.h"
#include "debfile.h"
#include "installer.h"
#include "reviews.h"
#include "searchsuggestions.h"
#include "storesettings.h"
#include "uninstallcommand.h"

#include <KAboutData>
#include <KDBusService>
#include <KLocalizedContext>
#include <KLocalizedString>
#include <KNotification>
#include <KWindowSystem>
#include <QApplication>
#include <QCommandLineParser>
#include <QFile>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickWindow>

#include <algorithm>
#include <functional>

#ifdef __GLIBC__
#include <malloc.h>
#endif

namespace
{

// What the window is asked to show; the QML side follows openApp().
class Requests : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString startApp MEMBER m_startApp CONSTANT)
public:
    explicit Requests(const QString &startApp, QObject *parent = nullptr)
        : QObject(parent)
        , m_startApp(startApp)
    {
    }

Q_SIGNALS:
    // an appstream:// link or an app id came in while the Store runs
    void openApp(const QString &appId);

private:
    QString m_startApp;
};

// Hands the memory of pages that were closed back to the system; glibc
// keeps freed blocks for later otherwise, and the Store would stay at
// the size of the biggest page it ever showed.
class Memory : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
    Q_INVOKABLE void release()
    {
#ifdef __GLIBC__
        malloc_trim(0);
#endif
    }
};

void notify(const QString &event, const QString &title, const QString &text, const QString &icon,
            std::function<void()> onClick = {})
{
    auto *n = new KNotification(event, KNotification::CloseOnTimeout);
    n->setComponentName(QStringLiteral("raye-store"));
    n->setTitle(title);
    n->setText(text);
    n->setIconName(icon.isEmpty() || icon.contains(QLatin1Char('/')) ? QStringLiteral("system-software-install") : icon);
    if (onClick) {
        KNotificationAction *action = n->addDefaultAction(i18n("Open Store"));
        QObject::connect(action, &KNotificationAction::activated, n, onClick);
    }
    n->sendEvent();
}

} // namespace

int main(int argc, char *argv[])
{
#ifdef __GLIBC__
    // Big blocks (pictures, icons) come straight from the system and go back
    // when freed, and the threads that read the catalog and decode pictures
    // share two pools instead of keeping one each.
    mallopt(M_MMAP_THRESHOLD, 128 * 1024);
    mallopt(M_TRIM_THRESHOLD, 256 * 1024);
    mallopt(M_ARENA_MAX, 2);
#endif
    // no window to show: it runs where there is no display too
    if (std::any_of(argv, argv + argc, [](const char *a) { return qstrncmp(a, "--uninstall", 11) == 0; }))
        qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    // the pages make their own rows: a category, a feed, a sort order
    qmlRegisterType<CatalogFilter>("org.raye.store.data", 1, 0, "CatalogFilter");
    qmlRegisterType<SearchSuggestions>("org.raye.store.data", 1, 0, "SearchSuggestions");
    qmlRegisterType<Reviews>("org.raye.store.data", 1, 0, "Reviews");

    // used by the build tests: load the interface, report, quit
    if (app.arguments().contains(QStringLiteral("--check-qml"))) {
        QQmlEngine engine;
        bool broken = false;
        for (const char *root : {"Main", "DebInstall"}) {
            QQmlComponent component(&engine);
            component.loadFromModule("org.raye.store", root);
            for (const auto &error : component.errors())
                qCritical("%s", qPrintable(error.toString()));
            broken = broken || component.isError();
        }
        return broken ? 1 : 0;
    }
    KLocalizedString::setApplicationDomain("raye-store");
    QApplication::setWindowIcon(QIcon::fromTheme(QStringLiteral("system-software-install")));

    KAboutData about(QStringLiteral("raye-store"), i18n("Store"),
                     QStringLiteral(RAYE_STORE_VERSION),
                     i18n("Apps for Raye Linux"), KAboutLicense::GPL_V3);
    about.setOrganizationDomain("raye.org");
    about.setDesktopFileName(QStringLiteral("org.raye.store"));
    KAboutData::setApplicationData(about);

    QCommandLineParser parser;
    about.setupCommandLine(&parser);
    parser.addPositionalArgument(QStringLiteral("app"), i18n("App to show: its id, or an appstream:// link"));
    const QCommandLineOption openOption(QStringLiteral("open"), i18n("Open the page of this app"), QStringLiteral("app"));
    const QCommandLineOption uninstallOption(
        QStringLiteral("uninstall"), i18n("Remove this app, with no window, and print how it goes as JSON lines"), QStringLiteral("app"));
    const QCommandLineOption installFileOption(
        QStringLiteral("install-file"), i18n("Open the small window that installs this .deb file"), QStringLiteral("file"));
    parser.addOption(openOption);
    parser.addOption(uninstallOption);
    parser.addOption(installFileOption);
    parser.process(app);
    about.processCommandLine(&parser);

    // no window, no D-Bus name: Settings runs this while the Store may be open
    if (parser.isSet(uninstallOption)) {
        Installer installer;
        QFile out;
        if (!out.open(stdout, QIODevice::WriteOnly | QIODevice::Unbuffered))
            return 1;
        UninstallCommand command(&installer, &out);
        QObject::connect(&command, &UninstallCommand::done, &app, &QCoreApplication::exit, Qt::QueuedConnection);
        command.start(resolveUninstallTarget(parser.value(uninstallOption)));
        return app.exec();
    }

    // a .deb someone opened: its own small window, one per file, beside
    // the Store and never handed to it
    if (parser.isSet(installFileOption)) {
        QGuiApplication::setDesktopFileName(QStringLiteral("org.raye.store.install"));
        DebFile deb(parser.value(installFileOption));
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextObject(new KLocalizedContext(&engine));
        engine.rootContext()->setContextProperty(QStringLiteral("deb"), &deb);
        engine.loadFromModule("org.raye.store", "DebInstall");
        if (engine.rootObjects().isEmpty())
            return 1;
        deb.load();
        return app.exec();
    }

    // one Store: a second start hands its arguments to the running one
    // (which may be installing with its window closed) and leaves
    KDBusService service(KDBusService::Unique);

    // the window hides while installs go on; the Store quits itself
    QApplication::setQuitOnLastWindowClosed(false);

    Catalog catalog;
    CatalogFilter browse, library;
    browse.setSourceModel(&catalog);
    library.setSourceModel(&catalog);
    library.setInstalledOnly(true);
    Installer installer;
    Background background(&installer);
    Requests requests(appFromArguments(parser.isSet(openOption) ? QStringList{parser.value(openOption)} : parser.positionalArguments()));
    QObject::connect(&installer, &Installer::finished, &catalog, [&catalog, &installer] {
        catalog.refreshInstalled();
        installer.checkUpdates();
    });
    // the installer has already checked the app is there (or gone): Open
    // and Uninstall come at once, not after every source is read again
    QObject::connect(&installer, &Installer::jobFinished, &catalog, [&catalog](const QVariantMap &job) {
        if (job.value(QStringLiteral("state")).toInt() != Installer::Done)
            return;
        const QString action = job.value(QStringLiteral("action")).toString();
        catalog.noteInstalled(job.value(QStringLiteral("ref")).toString(), action != QLatin1String("remove"));
    });
    QObject::connect(&background, &Background::quitRequested, &app, &QCoreApplication::quit, Qt::QueuedConnection);

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextObject(new KLocalizedContext(&engine));
    engine.rootContext()->setContextProperty(QStringLiteral("catalog"), &catalog);
    engine.rootContext()->setContextProperty(QStringLiteral("browse"), &browse);
    engine.rootContext()->setContextProperty(QStringLiteral("library"), &library);
    engine.rootContext()->setContextProperty(QStringLiteral("installer"), &installer);
    engine.rootContext()->setContextProperty(QStringLiteral("keeper"), &background);
    engine.rootContext()->setContextProperty(QStringLiteral("requests"), &requests);
    StoreSettings settings;
    engine.rootContext()->setContextProperty(QStringLiteral("storeSettings"), &settings);
    Memory memory;
    engine.rootContext()->setContextProperty(QStringLiteral("memory"), &memory);
    engine.rootContext()->setContextProperty(QStringLiteral("startApp"), requests.property("startApp"));
    engine.loadFromModule("org.raye.store", "Main");
    if (engine.rootObjects().isEmpty())
        return 1;
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());

    const auto bringBack = [window, &background] {
        if (!window)
            return;
        background.windowShown();
        window->show();
        KWindowSystem::updateStartupId(window);
        KWindowSystem::activateWindow(window);
    };
    QObject::connect(&service, &KDBusService::activateRequested, &app,
                     [&requests, bringBack](const QStringList &arguments, const QString &) {
                         bringBack();
                         // the first argument is the program
                         const QString id = appFromArguments(arguments.mid(1));
                         if (!id.isEmpty())
                             Q_EMIT requests.openApp(id);
                     });

    // the window closed while apps install: say they go on
    QObject::connect(&background, &Background::keptRunning, &app, [bringBack](const QStringList &names) {
        const QString text = names.size() == 1
            ? i18n("%1 is still installing. We'll let you know when it's ready.", names.first())
            : i18np("One app is still installing. We'll let you know when it's ready.",
                    "%1 apps are still installing. We'll let you know when they're ready.", names.size());
        notify(QStringLiteral("working"), i18n("Store"), text, QString(), bringBack);
    });
    // the end of a job, when the window is not in front
    QObject::connect(&installer, &Installer::jobFinished, &app, [window, &background, bringBack](const QVariantMap &job) {
        if (window && window->isVisible() && window->isActive() && !background.hidden())
            return;
        const int state = job.value(QStringLiteral("state")).toInt();
        const QString name = job.value(QStringLiteral("name")).toString();
        const QString action = job.value(QStringLiteral("action")).toString();
        const QString icon = job.value(QStringLiteral("icon")).toString();
        if (state == Installer::Done && action == QLatin1String("install"))
            notify(QStringLiteral("ready"), i18n("%1 is ready", name), i18n("Find it in Start."), icon);
        else if (state == Installer::Failed)
            notify(QStringLiteral("failed"),
                   action == QLatin1String("remove")   ? i18n("We couldn't remove %1", name)
                   : action == QLatin1String("update") ? i18n("We couldn't update %1", name)
                                                       : i18n("We couldn't install %1", name),
                   job.value(QStringLiteral("reasonText")).toString(), icon, bringBack);
    });

    // what is on disk first, so the window fills at once; again when the
    // fresh metadata has arrived
    QObject::connect(&installer, &Installer::catalogRefreshed, &catalog, &Catalog::reload);
    catalog.reload();
    catalog.watchInstalled();
    installer.refreshCatalog();
    installer.checkUpdates();
    return app.exec();
}

#include "main.moc"
