// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import org.raye.store
import org.raye.store.data

Kirigami.ApplicationWindow {
    id: window
    title: i18n("Store")
    width: 1280
    height: 820
    minimumWidth: 640
    minimumHeight: 500
    // the same colour as the Raye title bar, so the two read as one bar
    color: Theme.backdropSolid
    pageStack.globalToolBar.style: Kirigami.ApplicationHeaderStyle.None

    property string notice: ""

    // where we are: { page: "home" | "apps" | "games" | "library" | "settings"
    // | "app" | "list" | "search", ... }, and how we got here
    property var route: ({ page: "home" })
    property var history: []
    // the Raye account: its button and "Your devices" wait, hidden, for
    // an account service; there is none yet
    readonly property bool accountAvailable: false
    // the rail place a page belongs to
    property string section: "home"

    // the pages Back left, for Forward (like a browser)
    property var ahead: []
    function go(next) {
        if (next.page === "search" && route.page === "search") {
            route = next
            return
        }
        history = history.concat([route])
        ahead = []
        route = next
    }
    function back() {
        if (history.length === 0)
            return
        const previous = history[history.length - 1]
        history = history.slice(0, -1)
        ahead = ahead.concat([route])
        route = previous
    }
    function forward() {
        if (ahead.length === 0)
            return
        const next = ahead[ahead.length - 1]
        ahead = ahead.slice(0, -1)
        history = history.concat([route])
        route = next
    }
    function goSection(page) {
        if (route.page === page) {
            pages.current().toTop()
            return
        }
        go({ page: page })
    }
    function showApp(app) { go({ page: "app", app: app }) }
    onRouteChanged: {
        if (["home", "apps", "games", "library", "settings"].indexOf(route.page) >= 0)
            section = route.page
        // the box says what the page shows; a search that is being typed
        // keeps its spaces
        const query = route.page === "search" ? route.query : ""
        if (searchBox.text.trim() !== query)
            searchBox.text = query
        suggest.forget()
        pages.enter()
    }

    // installs go on with the window closed: it only hides, and the
    // Store quits once the last one is done
    onClosing: close => {
        if (keeper.windowClosing()) {
            close.accepted = false
            window.hide()
        }
    }

    // raye-store <app id> or an appstream:// link opens straight on that
    // app, once the list is in; also when the Store was already running
    property string pendingApp: startApp
    function openPending() {
        if (catalog.loading || pendingApp.length === 0)
            return
        const app = catalog.find(pendingApp)
        if (app.appId === undefined) {
            // the fresh list is in and it is not there: say so once (the
            // one kept from last time may just be older)
            if (catalog.rowCount() > 0 && catalog.settled) {
                pendingApp = ""
                notice = i18n("This app isn't available right now.")
            }
            return
        }
        pendingApp = ""
        window.showApp(app)
    }
    Connections {
        target: catalog
        function onSettledChanged() { window.openPending() }
        function onLoadingChanged() {
            window.openPending()
            // reading the catalog leaves a lot of freed memory behind, and
            // so does filling Home (rows, icons, the featured pictures)
            if (!catalog.loading) {
                collectLater.restart()
                settleLater.restart()
            }
        }
    }
    Connections {
        target: requests
        function onOpenApp(appId) {
            window.pendingApp = appId
            window.openPending()
        }
    }
    Connections {
        target: installer
        function onJobFinished(job) {
            // 6: Failed; a cancel is not a failure. The app's own page
            // says it under its button: once is enough
            const shownThere = window.route.page === "app" && window.route.app.ref === job.ref
            window.notice = job.state === 6 && !shownThere ? i18n("That did not work: %1", job.reasonText) : ""
        }
    }
    Component.onCompleted: {
        // names, icons and notes for the updates list
        installer.setCatalog(catalog)
        openPending()
    }

    Timer {
        id: settleLater
        interval: 10000
        onTriggered: {
            gc()
            memory.release()
        }
    }

    Shortcut { sequences: ["Ctrl+F", "Ctrl+E"]; onActivated: searchBox.forceActiveFocus() }
    Shortcut { sequences: [StandardKey.Back, "Alt+Left"]; onActivated: window.back() }
    Shortcut { sequences: [StandardKey.Forward, "Alt+Right"]; onActivated: window.forward() }
    Shortcut {
        sequence: "Backspace"
        // only when no field is taking the key
        enabled: !(window.activeFocusItem && window.activeFocusItem.cursorPosition !== undefined)
        onActivated: window.back()
    }
    // the mouse's back and forward buttons, anywhere in the window
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.BackButton | Qt.ForwardButton
        z: 100
        propagateComposedEvents: true
        onPressed: mouse => {
            if (mouse.button === Qt.BackButton)
                window.back()
            else
                window.forward()
        }
    }
    Shortcut { sequence: "Backspace"; enabled: !searchBox.activeFocus; onActivated: window.back() }

    // typing offers apps under the box; Enter or "See all" searches
    SearchSuggestions {
        id: suggest
        source: catalog
        showSystemPackages: storeSettings.showSystemPackages
        onOpenApp: appId => {
            const app = catalog.find(appId)
            if (app.appId !== undefined)
                window.showApp(app)
        }
        onShowResults: query => window.go({ page: "search", query: query })
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // the navigation rail, like the store people know
        Item {
            id: rail
            Layout.fillHeight: true
            Layout.preferredWidth: Theme.railWidth

            readonly property Item selectedItem: window.section === "home" ? homeItem
                                               : window.section === "apps" ? appsItem
                                               : window.section === "games" ? gamesItem
                                               : window.section === "settings" ? settingsItem : libraryItem

            ColumnLayout {
                anchors.fill: parent
                anchors.topMargin: Theme.headerHeight + Theme.s1
                anchors.bottomMargin: Theme.s3
                spacing: Theme.s2
                NavItem {
                    id: homeItem
                    Layout.alignment: Qt.AlignHCenter
                    text: i18n("Home")
                    glyph: "home"
                    selected: window.section === "home"
                    onClicked: window.goSection("home")
                }
                NavItem {
                    id: appsItem
                    Layout.alignment: Qt.AlignHCenter
                    text: i18nc("@title navigation", "Apps")
                    glyph: "apps"
                    selected: window.section === "apps"
                    onClicked: window.goSection("apps")
                }
                NavItem {
                    id: gamesItem
                    Layout.alignment: Qt.AlignHCenter
                    text: i18nc("@title navigation", "Games")
                    glyph: "games"
                    selected: window.section === "games"
                    onClicked: window.goSection("games")
                }
                Item { Layout.fillHeight: true }
                NavItem {
                    id: libraryItem
                    Layout.alignment: Qt.AlignHCenter
                    text: i18n("Library")
                    glyph: "library"
                    selected: window.section === "library"
                    badgeCount: installer.flatpakUpdates
                    busy: installer.busy
                    Accessible.description: installer.flatpakUpdates > 0 ? i18np("One update", "%1 updates", installer.flatpakUpdates) : ""
                    onClicked: window.goSection("library")
                }
                NavItem {
                    id: settingsItem
                    Layout.alignment: Qt.AlignHCenter
                    text: i18nc("@title navigation", "Settings")
                    glyph: "settings"
                    selected: window.section === "settings"
                    onClicked: window.goSection("settings")
                }
            }

            // the accent pill: slides to the place you are on
            Rectangle {
                width: Theme.pillWidth
                height: Theme.pillHeight
                radius: width / 2
                x: (rail.width - Theme.railItemWidth) / 2 + 2
                y: rail.selectedItem ? rail.selectedItem.y + Theme.headerHeight + Theme.s1
                                       + (rail.selectedItem.height - height) / 2 : 0
                gradient: Gradient {
                    GradientStop { position: 0; color: Theme.accent }
                    GradientStop { position: 1; color: Theme.accentGlowEnd }
                }
                Behavior on y { NumberAnimation { duration: Theme.durationFast + 40; easing.type: Theme.easingEnter } }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // the header: Back, and the search box in the middle. Above the
            // content, which the suggestions open over.
            Item {
                z: 1
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.headerHeight
                Button {
                    id: backButton
                    subtle: true
                    glyph: "back"
                    implicitWidth: 40
                    implicitHeight: 36
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    visible: window.history.length > 0
                    Accessible.name: i18n("Back")
                    onClicked: window.back()
                }
                SearchBox {
                    id: searchBox
                    width: Math.min(Theme.searchMaxWidth, parent.width - 2 * 64)
                    anchors.centerIn: parent
                    anchors.horizontalCenterOffset: -Theme.railWidth / 2
                    suggestionsShown: suggest.shown
                    onTextEdited: {
                        suggest.type(text)
                        if (text.trim() === "" && window.route.page === "search")
                            window.back()
                    }
                    onSearchNow: { suggest.current = -1; suggest.accept() }
                    onAccepted: suggest.accept()
                    onDismissSuggestions: suggest.dismiss()
                    onActiveFocusChanged: if (!activeFocus) suggest.dismiss()
                    Keys.onDownPressed: {
                        if (suggest.shown)
                            suggest.down()
                        else
                            pages.current().forceActiveFocus()
                    }
                    Keys.onUpPressed: suggest.up()
                }
                SuggestionsFlyout {
                    id: flyout
                    anchors.left: searchBox.left
                    anchors.right: searchBox.right
                    anchors.top: searchBox.bottom
                    anchors.topMargin: Theme.s2
                    suggestions: suggest
                }
                Button {
                    visible: window.accountAvailable
                    subtle: true
                    glyph: "person"
                    glyphSize: 18
                    implicitWidth: 40
                    implicitHeight: 36
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.s4
                    anchors.verticalCenter: parent.verticalCenter
                    Accessible.name: i18nc("@action:button the Raye account", "Account")
                }
            }

            // the content: a layer above the window colour, its top left
            // corner rounded where it meets the rail and the header
            Rectangle {
                id: content
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.rightMargin: -radius
                Layout.bottomMargin: -radius
                radius: Theme.radiusCard
                color: Theme.layerSolid
                border.width: 1
                border.color: Theme.strokeSurface
                clip: true

                Item {
                    id: pages
                    anchors.fill: parent
                    anchors.margins: 1
                    anchors.rightMargin: content.radius + 1
                    anchors.bottomMargin: content.radius + 1

                    // a page is kept while it is shown or one Back away; the
                    // others are let go, and their memory with them (Back makes
                    // them again from the history)
                    function inUse(page) {
                        const previous = window.history.length > 0 ? window.history[window.history.length - 1].page : ""
                        return window.route.page === page || previous === page
                    }
                    function current() {
                        switch (window.route.page) {
                        case "apps": return appsLoader.item
                        case "games": return gamesLoader.item
                        case "library": return libraryLoader.item
                        case "settings": return settingsLoader.item
                        case "app": return appLoader.item
                        case "list": case "search": return listLoader.item
                        }
                        return homePage
                    }
                    // the page that comes in rises a little and fades in; what it
                    // shows is set a moment later, once its Loader has made it
                    function enter() {
                        entrance.restart()
                        Qt.callLater(fill)
                        // the pages let go above leave their objects to collect
                        collectLater.restart()
                    }
                    Timer {
                        id: collectLater
                        interval: 2000
                        onTriggered: {
                            gc()
                            memory.release()
                        }
                    }
                    function fill() {
                        const r = window.route
                        if (r.page === "app")
                            appLoader.item.app = r.app
                        if (r.page === "list" || r.page === "search") {
                            const list = listLoader.item
                            list.mode = r.page === "search" ? "search" : "row"
                            list.title = r.title || ""
                            list.spec = r.spec || ({})
                            list.kind = r.kind || ""
                            list.query = r.query || ""
                            list.apply()
                            // after the grid has taken in the new rows
                            Qt.callLater(list.toTop)
                        }
                    }
                    function showRow(spec) {
                        window.go({ page: "list", title: spec.title, spec: spec })
                    }
                    ParallelAnimation {
                        id: entrance
                        NumberAnimation { target: pages; property: "opacity"; from: 0; to: 1; duration: Theme.durationNormal; easing.type: Theme.easingEnter }
                        NumberAnimation { target: slide; property: "y"; from: 24; to: 0; duration: Theme.durationNormal + 60; easing.type: Theme.easingEnter }
                    }
                    transform: Translate { id: slide }

                    HomePage {
                        id: homePage
                        anchors.fill: parent
                        visible: window.route.page === "home"
                        onOpen: app => window.showApp(app)
                        onShowRow: spec => pages.showRow(spec)
                    }
                    Loader {
                        id: appsLoader
                        anchors.fill: parent
                        active: pages.inUse("apps")
                        visible: window.route.page === "apps"
                        sourceComponent: BrowsePage {
                            kind: "app"
                            onOpen: app => window.showApp(app)
                            onShowRow: spec => pages.showRow(spec)
                        }
                    }
                    Loader {
                        id: gamesLoader
                        anchors.fill: parent
                        active: pages.inUse("games")
                        visible: window.route.page === "games"
                        sourceComponent: BrowsePage {
                            kind: "game"
                            onOpen: app => window.showApp(app)
                            onShowRow: spec => pages.showRow(spec)
                        }
                    }
                    Loader {
                        id: libraryLoader
                        anchors.fill: parent
                        active: pages.inUse("library")
                        visible: window.route.page === "library"
                        sourceComponent: LibraryPage {
                            accountAvailable: window.accountAvailable
                            onOpen: app => window.showApp(app)
                            onBrowseApps: window.goSection("apps")
                        }
                    }
                    Loader {
                        id: settingsLoader
                        anchors.fill: parent
                        active: pages.inUse("settings")
                        visible: window.route.page === "settings"
                        sourceComponent: SettingsPage {}
                    }
                    Loader {
                        id: listLoader
                        anchors.fill: parent
                        active: pages.inUse("list") || pages.inUse("search")
                        visible: window.route.page === "list" || window.route.page === "search"
                        sourceComponent: ListPage {
                            onOpen: app => window.showApp(app)
                        }
                    }
                    Loader {
                        id: appLoader
                        anchors.fill: parent
                        active: pages.inUse("app")
                        visible: window.route.page === "app"
                        sourceComponent: AppPage {
                            onBack: window.back()
                            onOpen: app => window.showApp(app)
                        }
                    }
                }

                InfoBar {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: Theme.s4
                    anchors.rightMargin: content.radius + Theme.s4
                    visible: window.notice.length > 0
                    text: window.notice
                    onClosed: window.notice = ""
                }
            }
        }
    }

    // a press elsewhere closes the flyout and does nothing more; the box
    // and the flyout itself keep theirs
    MouseArea {
        anchors.fill: parent
        visible: suggest.shown
        acceptedButtons: Qt.AllButtons
        onPressed: mouse => {
            const inFlyout = flyout.contains(mapToItem(flyout, mouse.x, mouse.y))
            const inBox = searchBox.contains(mapToItem(searchBox, mouse.x, mouse.y))
            if (!inFlyout && !inBox)
                suggest.dismiss()
            mouse.accepted = !inFlyout && !inBox
        }
    }
}
