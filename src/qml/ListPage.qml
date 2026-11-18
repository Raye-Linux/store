// SPDX-License-Identifier: GPL-3.0-or-later
// A grid of apps: a row's "See all" or a category (sorted by popularity,
// rating, date or name), or what was searched (narrowed to apps or
// games).
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.raye.store

Item {
    id: listPage

    // "row" (a row's rule, below) or "search"
    property string mode: "row"
    property string title
    // { kind, category, ids, sort, minRatings }, as for a SectionRow
    property var spec: ({})
    property string kind: ""
    // for a search
    property string query: ""
    signal open(var app)

    // the shared filter follows the page that shows it
    function apply() {
        changedAt = Date.now()
        const row = mode === "row"
        browse.limit = 0
        browse.showSystemPackages = !row && storeSettings.showSystemPackages
        browse.text = row ? "" : query
        browse.ids = row && spec.ids ? spec.ids : []
        browse.category = row && spec.category ? spec.category : ""
        browse.minRatingCount = row && spec.minRatings ? spec.minRatings : 0
        browse.kind = row ? (spec.kind || "") : kind
        browse.sortRole = row && spec.sort ? spec.sort : "popularity"
    }
    onModeChanged: apply()
    onQueryChanged: apply()
    onSpecChanged: apply()
    onKindChanged: apply()
    function toTop() { grid.positionViewAtBeginning() }
    // new rows, or the same in another order, right after a search or a
    // sort: start from the top. An install finishing later moves nothing.
    property double changedAt: 0
    function settle() { if (Date.now() - changedAt < 1500) Qt.callLater(toTop) }
    Connections {
        target: browse
        function onModelReset() { listPage.settle() }
        function onLayoutChanged() { listPage.settle() }
        function onCountChanged() { listPage.settle() }
    }

    readonly property real pad: Theme.contentPadding
    readonly property real usable: Math.min(width - 2 * pad, Theme.contentMaxWidth)
    readonly property int columns: Math.max(1, Math.floor((usable + Theme.rowSpacing) / (Theme.cardWidth + Theme.rowSpacing)))
    readonly property int total: browse.count

    // the title and the chips stay put; the cards scroll under them
    Item {
        id: header
        x: (parent.width - listPage.usable) / 2 - 8
        width: listPage.usable + 8
        height: headerColumn.implicitHeight + Theme.s3
        ColumnLayout {
            id: headerColumn
            x: 8
            width: listPage.usable
            spacing: Theme.s5

            ColumnLayout {
                Layout.topMargin: Theme.s7
                spacing: Theme.s1
                Text {
                    Layout.fillWidth: true
                    text: listPage.mode === "search" ? i18nc("@title search results", "Results for “%1”", listPage.query) : listPage.title
                    font: Theme.title
                    color: Theme.text
                    elide: Text.ElideRight
                }
                Text {
                    text: i18ncp("@info", "One app", "%1 apps", listPage.total)
                    // keeps its line when empty, so the header does not change height
                    opacity: listPage.total > 0 ? 1 : 0
                    font: Theme.body
                    color: Theme.textSecondary
                }
            }

            Flow {
                Layout.fillWidth: true
                Layout.bottomMargin: Theme.s4
                spacing: Theme.s3
                // curated rows keep their order
                visible: listPage.mode === "search" || !(listPage.spec.ids && listPage.spec.ids.length > 0)
                Repeater {
                    model: listPage.mode === "search"
                           ? [["", i18nc("@option:radio search in", "All")], ["app", i18nc("@option:radio search in", "Apps")],
                              ["game", i18nc("@option:radio search in", "Games")]]
                           : [["popularity", i18nc("@option:radio sort by", "Most popular")], ["rating", i18nc("@option:radio sort by", "Top rated")],
                              ["newest", i18nc("@option:radio sort by", "Newest")], ["name", i18nc("@option:radio sort by", "Name")]]
                    delegate: Chip {
                        required property var modelData
                        text: modelData[1]
                        selected: listPage.mode === "search" ? browse.kind === modelData[0] : browse.sortRole === modelData[0]
                        onClicked: {
                            listPage.changedAt = Date.now()
                            if (listPage.mode === "search")
                                listPage.kind = modelData[0]
                            else
                                browse.sortRole = modelData[0]
                        }
                    }
                }
            }
        }
    }

    GridView {
        id: grid
        WheelScroll { target: grid }
        // 8 px either side so the cards' shadows are not cut
        width: listPage.usable + Theme.rowSpacing + 16
        y: header.height
        height: parent.height - header.height
        x: (parent.width - listPage.usable) / 2 - 8
        bottomMargin: listPage.pad
        cellWidth: (listPage.usable + Theme.rowSpacing) / listPage.columns
        cellHeight: Theme.cardHeight + Theme.rowSpacing
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        // no current card: the view would scroll to it, past the title
        currentIndex: -1
        highlightFollowsCurrentItem: false
        model: browse
        QQC2.ScrollBar.vertical: ThinScrollBar {
            parent: listPage
            x: listPage.width - width
            y: header.height
            height: listPage.height - header.height
        }

        delegate: Item {
            id: cell
            required property var model
            required property int index
            readonly property var app: model.modelData !== undefined ? model.modelData : model
            width: grid.cellWidth
            height: grid.cellHeight
            AppCard {
                x: 8
                y: 4
                width: grid.cellWidth - Theme.rowSpacing
                height: Theme.cardHeight
                app: cell.app
                windowsRow: listPage.mode !== "search" && listPage.spec !== null && listPage.spec !== undefined && listPage.spec.windows === true
                onClicked: listPage.open(catalog.find(cell.app.appId))
            }
        }

    }

    // the cards fade out under the title instead of being cut
    Rectangle {
        y: header.height
        width: parent.width - 12
        height: 20
        opacity: grid.contentY > grid.originY + 2 ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: Theme.durationFast } }
        gradient: Gradient {
            GradientStop { position: 0; color: Theme.layerSolid }
            GradientStop { position: 1; color: Qt.alpha(Theme.layerSolid, 0) }
        }
    }

    // not in the grid's header: a header that changes height moves the view
    EmptyState {
        anchors.horizontalCenter: parent.horizontalCenter
        y: 200
        width: Math.min(parent.width - 2 * listPage.pad, 560)
        visible: listPage.total === 0 && listPage.mode === "search" && !catalog.loading
        glyph: "eye"
        title: i18nc("@title", "No results for “%1”", listPage.query)
        message: listPage.kind !== "" ? i18n("Try searching in all apps and games, or check the spelling.")
                                      : i18n("Check the spelling, or try a shorter or different word.")
        Button {
            visible: listPage.kind !== ""
            text: i18nc("@action:button", "Search everything")
            onClicked: listPage.kind = ""
        }
    }
}
