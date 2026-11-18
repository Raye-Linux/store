// SPDX-License-Identifier: GPL-3.0-or-later
// Home: the featured apps, then many rows and the collections, each
// made only when the page scrolls near it.
import QtQuick
import QtQuick.Layouts
import org.raye.store
import "ui/rows.js" as Rows
import "ui/hero.js" as Hero

PageScroll {
    id: home

    signal open(var app)
    signal showRow(var spec)

    property var featured: []
    // goes up when the feeds are read again: the rows that use them follow
    property int feedRevision: 0
    // a row built on a feed that is not there yet is left out
    function ready(rows) { return feedRevision >= 0 ? rows.filter(r => r.ids === undefined || r.ids.length > 0) : [] }
    // the list has been read at least once and came back empty
    property bool loadedOnce: false
    readonly property bool empty: loadedOnce && !catalog.loading && catalog.rowCount() === 0

    function fill() {
        featured = Rows.pick(catalog, Hero.today(new Date()), app => Rows.hasScreenshot(app), 5)
    }
    Connections {
        target: catalog
        function onLoadingChanged() {
            if (catalog.loading)
                return
            home.loadedOnce = true
            home.feedRevision++
            home.fill()
        }
        function onFeedsChanged() {
            if (catalog.loading)
                return
            home.feedRevision++
            home.fill()
        }
    }

    // the rows before the collections, then the ones after
    readonly property var firstRows: ready([
        { title: i18nc("@title row of apps", "Top free apps"), kind: "app" },
        { title: i18nc("@title row of apps", "Trending"), ids: catalog.feed("trending") },
        { title: i18nc("@title row of apps", "Essential apps"), ids: Collections.essentials },
        { title: i18nc("@title row of apps", "Coming from Windows?"), ids: catalog.fromWindows(), instead: true }
    ])
    readonly property var gameRows: ready([
        { title: i18nc("@title row of apps", "Top free games"), kind: "game" }
    ])
    // Windows programs: the ones Raye checked one by one, then all of them
    readonly property var windowsRows: ready([
        { title: i18nc("@title row of Windows programs", "Optimized for Raye"), ids: catalog.windowsPrograms("optimized"), minimum: 1, windows: true },
        { title: i18nc("@title row of Windows programs", "Your Windows favorites"), ids: catalog.windowsPrograms(), windows: true }
    ])
    readonly property var moreRows: ready([
        { title: i18nc("@title row of apps", "New and notable"), ids: catalog.feed("recently-added") },
        { title: i18nc("@title row of apps", "Best rated"), kind: "app", sort: "rating", minRatings: 50 },
        { title: i18nc("@title row of apps", "Productivity"), kind: "app", category: "Office" },
        { title: i18nc("@title row of apps", "Photo and design"), kind: "app", category: "Graphics" },
        { title: i18nc("@title row of apps", "Music and audio"), kind: "app", category: "Audio" },
        { title: i18nc("@title row of apps", "Watch and listen"), kind: "app", category: "Player" },
        { title: i18nc("@title row of apps", "Social"), kind: "app", category: "InstantMessaging" },
        { title: i18nc("@title row of apps", "Developer tools"), kind: "app", category: "Development" },
        { title: i18nc("@title row of apps", "Utilities and tools"), kind: "app", category: "Utility" },
        { title: i18nc("@title row of apps", "Education"), kind: "app", category: "Education" },
        { title: i18nc("@title row of apps", "Best rated games"), kind: "game", sort: "rating", minRatings: 30 }
    ])
    readonly property var lastRows: ready([
        { title: i18nc("@title row of apps", "Recently updated"), ids: catalog.feed("recently-updated") }
    ])

    Hero {
        Layout.fillWidth: true
        visible: !home.empty
        apps: home.featured
        loading: catalog.loading
        onOpen: app => home.open(app)
    }

    Repeater {
        model: home.empty ? [] : home.firstRows
        delegate: SectionRow {
            required property var modelData
            spec: modelData
            page: home
            onOpen: app => home.open(app)
            onSeeAll: spec => home.showRow(spec)
        }
    }

    LauncherSpotlight {
        Layout.fillWidth: true
        visible: !home.empty && launchers.length > 0
        onOpen: app => home.open(app)
    }

    Repeater {
        model: home.empty ? [] : home.gameRows
        delegate: SectionRow {
            required property var modelData
            spec: modelData
            page: home
            onOpen: app => home.open(app)
            onSeeAll: spec => home.showRow(spec)
        }
    }

    Repeater {
        model: home.empty ? [] : home.windowsRows
        delegate: SectionRow {
            required property var modelData
            spec: modelData
            minimum: modelData.minimum || 4
            page: home
            onOpen: app => home.open(app)
            onSeeAll: spec => home.showRow(spec)
        }
    }

    LazyBlock {
        page: home
        visible: !home.empty
        placeholderHeight: 260
        sourceComponent: TileBlock {
            title: i18nc("@title", "Collections")
            tiles: Collections.all
            minimum: 4
            onShow: spec => home.showRow(spec)
        }
    }

    Repeater {
        model: home.empty ? [] : home.moreRows
        delegate: SectionRow {
            required property var modelData
            spec: modelData
            page: home
            onOpen: app => home.open(app)
            onSeeAll: spec => home.showRow(spec)
        }
    }

    LazyBlock {
        page: home
        visible: !home.empty
        placeholderHeight: 230
        sourceComponent: TileBlock {
            title: i18nc("@title", "Browse by category")
            tiles: Format.appCategories.map(c => ({ name: c.name, kind: "app", category: c.id }))
            onShow: spec => home.showRow(spec)
        }
    }

    Repeater {
        model: home.empty ? [] : home.lastRows
        delegate: SectionRow {
            required property var modelData
            spec: modelData
            page: home
            onOpen: app => home.open(app)
            onSeeAll: spec => home.showRow(spec)
        }
    }

    // no list of apps at all: nothing on disk and no network
    EmptyState {
        Layout.fillWidth: true
        Layout.topMargin: Math.max(80, home.height / 2 - 220)
        visible: home.empty
        glyph: "cloud-off"
        title: i18nc("@title", "The apps can't be shown right now")
        message: i18n("Raye couldn't load the list of apps. Check that you're connected to the internet, then try again.")
        Button {
            accent: true
            text: i18nc("@action:button", "Try again")
            onClicked: installer.refreshCatalog()
        }
    }
}
