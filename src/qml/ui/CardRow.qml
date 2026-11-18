// SPDX-License-Identifier: GPL-3.0-or-later
// A row of app cards under its title: scrolled with the round arrows
// that show on hover, or by moving the keyboard focus along it. Takes
// a list of apps or one of the catalog's filters.
import QtQuick
import QtQuick.Layouts

ColumnLayout {
    id: row

    property string title
    // maps from catalog.find(), or a CatalogFilter
    property var model: []
    // an app not to show (the page's own app)
    property string skipId
    // the catalog is still being read: grey cards stand in
    property bool loading: false
    // a row of Windows programs: native versions say they are native
    property bool windowsRow: false
    // a row of apps that do the job of Windows programs: the cards say which
    property bool insteadRow: false
    property bool showAll: count > 0 && !loading
    readonly property int count: Array.isArray(model) ? model.length : model ? model.count : 0
    signal open(var app)
    signal seeAll()

    spacing: Theme.s4

    SectionHeader {
        Layout.fillWidth: true
        title: row.title
        showAll: row.showAll
        onSeeAll: row.seeAll()
    }

    Item {
        id: strip
        Layout.fillWidth: true
        Layout.preferredHeight: Theme.cardHeight + 2 * Theme.s4
        // the cards' shadows go past the strip
        Layout.topMargin: -Theme.s4
        Layout.bottomMargin: -Theme.s4

        HoverHandler { id: hover }

        Row {
            visible: row.loading && row.count === 0
            y: Theme.s4
            spacing: Theme.rowSpacing
            Repeater {
                model: parent.visible ? Math.ceil(strip.width / (Theme.cardWidth + Theme.rowSpacing)) : 0
                delegate: Rectangle {
                    width: Theme.cardWidth
                    height: Theme.cardHeight
                    radius: Theme.radiusCard
                    color: Theme.cardSolid
                    border.width: 1
                    border.color: Theme.strokeSurface
                    Shimmer {
                        x: Theme.s2; y: Theme.s2
                        width: parent.width - 2 * Theme.s2
                        height: Theme.cardTile
                        radius: Theme.radiusCard - 2
                    }
                    Shimmer { x: Theme.s4; y: Theme.cardTile + Theme.s5; width: parent.width * 0.6; height: 12 }
                    Shimmer { x: Theme.s4; y: Theme.cardTile + Theme.s5 + 20; width: parent.width * 0.4; height: 10 }
                }
            }
        }

        ListView {
            id: list
            anchors.fill: parent
            orientation: ListView.Horizontal
            // the page scrolls with the wheel; this row with its arrows
            interactive: false
            clip: true
            // only the cards in view are made
            cacheBuffer: 0
            reuseItems: true
            model: row.model
            delegate: Item {
                id: cell
                required property var model
                required property int index
                readonly property var app: model.modelData !== undefined ? model.modelData : model
                readonly property bool skipped: row.skipId !== "" && app.appId === row.skipId
                width: skipped ? 0 : Theme.cardWidth + Theme.rowSpacing
                height: list.height
                AppCard {
                    visible: !cell.skipped
                    y: Theme.s4
                    app: cell.app
                    windowsRow: row.windowsRow
                    insteadRow: row.insteadRow
                    onClicked: row.open(catalog.find(cell.app.appId))
                    onActiveFocusChanged: if (activeFocus) list.positionViewAtIndex(cell.index, ListView.Contain)
                }
            }
            Behavior on contentX {
                NumberAnimation { duration: Theme.durationSlow; easing.type: Theme.easingEnter }
            }
        }

        RoundArrow {
            glyph: "chevron-left"
            anchors.left: parent.left
            anchors.leftMargin: -width / 2 + Theme.s2
            anchors.verticalCenter: parent.verticalCenter
            shown: hover.hovered && list.contentX > 1
            Accessible.name: i18nc("@action:button scroll a row of apps", "Previous")
            onClicked: list.contentX = Math.max(0, list.contentX - list.width * 0.8)
        }
        RoundArrow {
            glyph: "chevron-right"
            anchors.right: parent.right
            anchors.rightMargin: -width / 2 + Theme.s2
            anchors.verticalCenter: parent.verticalCenter
            shown: hover.hovered && list.contentX + list.width < list.contentWidth - Theme.rowSpacing - 1
            Accessible.name: i18nc("@action:button scroll a row of apps", "Next")
            onClicked: list.contentX = Math.min(list.contentWidth - list.width, list.contentX + list.width * 0.8)
        }
    }
}
