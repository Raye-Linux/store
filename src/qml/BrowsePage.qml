// SPDX-License-Identifier: GPL-3.0-or-later
// Apps or Games: their own featured apps, the categories as chips, then
// many rows, each made only when the page scrolls near it.
import QtQuick
import QtQuick.Layouts
import org.raye.store
import "ui/rows.js" as Rows
import "ui/hero.js" as Hero

PageScroll {
    id: browsePage

    // "app" or "game"
    property string kind: "app"
    signal open(var app)
    signal showRow(var spec)

    readonly property bool games: kind === "game"
    readonly property var categoryList: games ? Format.gameCategories : Format.appCategories

    property var featured: []
    property int feedRevision: 0
    function ready(rows) { return feedRevision >= 0 ? rows.filter(r => r.ids === undefined || r.ids.length > 0) : [] }

    function fill() {
        const popular = catalog.feed("popular")
        // apps: Raye's own list; games: the popular ones
        featured = Rows.pick(catalog, games ? popular.slice(0, 150) : Hero.today(new Date()),
                             app => app.kind === kind && Rows.hasScreenshot(app), 5)
        feedRevision++
    }
    Connections {
        target: catalog
        function onLoadingChanged() { if (!catalog.loading) browsePage.fill() }
        function onFeedsChanged() { if (!catalog.loading) browsePage.fill() }
    }
    Component.onCompleted: if (!catalog.loading) fill()

    readonly property var rows: ready(games ? [
        { title: i18nc("@title row of games", "More game launchers"), ids: Collections.launchers, instead: true },
        { title: i18nc("@title row of apps", "Top free games"), kind: "game" },
        { title: i18nc("@title row of apps", "Trending games"), kind: "game", ids: catalog.feed("trending") },
        { title: i18nc("@title row of apps", "Best rated games"), kind: "game", sort: "rating", minRatings: 30 },
        { title: i18nc("@title row of apps", "New games"), kind: "game", ids: catalog.feed("recently-added") },
        { title: i18nc("@title row of games", "Action"), kind: "game", category: "ActionGame" },
        { title: i18nc("@title row of games", "Strategy"), kind: "game", category: "StrategyGame" },
        { title: i18nc("@title row of games", "Racing"), kind: "game", ids: Collections.racing },
        { title: i18nc("@title row of games", "Puzzle and casual"), kind: "game", category: "LogicGame" },
        { title: i18nc("@title row of games", "Arcade"), kind: "game", category: "ArcadeGame" },
        { title: i18nc("@title row of games", "Shooters"), kind: "game", category: "Shooter" },
        { title: i18nc("@title row of games", "Adventure and role-playing"), kind: "game", category: "RolePlaying" },
        { title: i18nc("@title row of games", "Simulation"), kind: "game", category: "Simulation" },
        { title: i18nc("@title row of games", "Family"), kind: "game", category: "KidsGame" },
        { title: i18nc("@title row of games", "Play online with others"), kind: "game", ids: Collections.multiplayer },
        { title: i18nc("@title row of games", "Emulators"), kind: "game", category: "Emulator" },
        { title: i18nc("@title row of games", "Launchers and tools"), category: "PackageManager" },
        { title: i18nc("@title row of apps", "Recently updated"), kind: "game", ids: catalog.feed("recently-updated") }
    ] : [
        { title: i18nc("@title row of apps", "Top free apps"), kind: "app" },
        { title: i18nc("@title row of apps", "Trending apps"), kind: "app", ids: catalog.feed("trending") },
        { title: i18nc("@title row of apps", "Coming from Windows?"), ids: catalog.fromWindows(), instead: true },
        { title: i18nc("@title row of apps", "Best rated"), kind: "app", sort: "rating", minRatings: 50 },
        { title: i18nc("@title row of apps", "New and notable"), kind: "app", ids: catalog.feed("recently-added") },
        { title: i18nc("@title row of apps", "Productivity"), kind: "app", category: "Office" },
        { title: i18nc("@title row of apps", "Photo and design"), kind: "app", category: "Graphics" },
        { title: i18nc("@title row of apps", "Video"), kind: "app", category: "Video" },
        { title: i18nc("@title row of apps", "Music and audio"), kind: "app", category: "Audio" },
        { title: i18nc("@title row of apps", "Watch and listen"), kind: "app", category: "Player" },
        { title: i18nc("@title row of apps", "Internet"), kind: "app", category: "Network" },
        { title: i18nc("@title row of apps", "Social"), kind: "app", category: "InstantMessaging" },
        { title: i18nc("@title row of apps", "Developer tools"), kind: "app", category: "Development" },
        { title: i18nc("@title row of apps", "Education"), kind: "app", category: "Education" },
        { title: i18nc("@title row of apps", "Science"), kind: "app", category: "Science" },
        { title: i18nc("@title row of apps", "Money"), kind: "app", category: "Finance" },
        { title: i18nc("@title row of apps", "Security and privacy"), kind: "app", category: "Security" },
        { title: i18nc("@title row of apps", "Utilities and tools"), kind: "app", category: "Utility" },
        { title: i18nc("@title row of apps", "Recently updated"), kind: "app", ids: catalog.feed("recently-updated") }
    ])

    ColumnLayout {
        Layout.fillWidth: true
        spacing: Theme.s2
        Text {
            text: browsePage.games ? i18nc("@title page", "Games") : i18nc("@title page", "Apps")
            font: Theme.title
            color: Theme.text
        }
        Text {
            text: browsePage.games ? i18n("Free games, and the launchers for the ones you already own.")
                                   : i18n("Every app here is free.")
            font: Theme.body
            color: Theme.textSecondary
        }
    }

    Hero {
        Layout.fillWidth: true
        Layout.topMargin: -Theme.s4
        apps: browsePage.featured
        loading: catalog.loading
        onOpen: app => browsePage.open(app)
    }

    Flow {
        Layout.fillWidth: true
        Layout.topMargin: -Theme.s4
        spacing: Theme.s3
        Repeater {
            model: browsePage.categoryList
            delegate: Chip {
                required property var modelData
                text: modelData.name
                onClicked: browsePage.showRow({ title: modelData.name, kind: browsePage.kind, category: modelData.id })
            }
        }
    }

    LauncherSpotlight {
        Layout.fillWidth: true
        visible: browsePage.games && launchers.length > 0
        onOpen: app => browsePage.open(app)
    }

    Repeater {
        model: browsePage.rows
        delegate: SectionRow {
            required property var modelData
            spec: modelData
            page: browsePage
            onOpen: app => browsePage.open(app)
            onSeeAll: spec => browsePage.showRow(spec)
        }
    }

    LazyBlock {
        page: browsePage
        visible: !browsePage.games
        placeholderHeight: 260
        sourceComponent: TileBlock {
            title: i18nc("@title", "Collections")
            tiles: Collections.all
            minimum: 4
            onShow: spec => browsePage.showRow(spec)
        }
    }
}
