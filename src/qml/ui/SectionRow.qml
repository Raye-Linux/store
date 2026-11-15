// SPDX-License-Identifier: GPL-3.0-or-later
// One row of a page, made only when the page scrolls near it: until
// then it is an empty space of the same height. Its apps come from the
// catalog by the row's rule (a category, a feed, a sort order); a row
// with too few apps stays hidden.
import QtQuick
import QtQuick.Layouts
import org.raye.store.data

Item {
    id: section

    // { title, kind, category, ids, sort, minRatings }: which apps, in
    // which order (sort: popularity, rating, newest or name)
    property var spec: ({})
    // the page that scrolls, to know when this row comes near
    property Flickable page
    property int minimum: 4
    signal open(var app)
    signal seeAll(var spec)

    // made once, when it comes within a screen of the view; until then an
    // empty space of the same height, so nothing moves. (Letting rows go
    // again when far away gave back little and cost a lot while scrolling.)
    readonly property real rowTop: page ? y + page.topPadding : 0
    readonly property bool near: page && rowTop < page.contentY + 2 * page.height
    property bool made: false
    onNearChanged: if (near) made = true
    Component.onCompleted: if (near) made = true
    property real lastHeight: Theme.sectionHeight
    // a row found too short stays hidden (until the catalog is read again)
    property bool tooFew: false
    Connections {
        target: catalog
        function onLoadingChanged() { section.tooFew = false }
    }

    readonly property bool enough: !tooFew && (!made || catalog.loading || !loader.item || loader.item.count >= minimum)
    Layout.fillWidth: true
    implicitHeight: !enough ? 0 : made && loader.item ? loader.item.implicitHeight : lastHeight
    visible: enough
    onImplicitHeightChanged: if (made && loader.item && implicitHeight > 0) lastHeight = implicitHeight

    Loader {
        id: loader
        width: parent.width
        active: section.made
        onLoaded: if (!catalog.loading && item.count < section.minimum) section.tooFew = true
        sourceComponent: CardRow {
            title: section.spec.title
            windowsRow: section.spec.windows === true
            loading: catalog.loading
            model: CatalogFilter {
                kind: section.spec.kind || ""
                category: section.spec.category || ""
                ids: section.spec.ids || []
                sortRole: section.spec.sort || "popularity"
                minRatingCount: section.spec.minRatings || 0
                limit: 20
                // set last, so the rules above rank the catalog once
                Component.onCompleted: sourceModel = catalog
            }
            onOpen: app => section.open(app)
            onSeeAll: section.seeAll(section.spec)
            onCountChanged: if (!catalog.loading && count < section.minimum && model.sourceModel) section.tooFew = true
        }
    }
}
