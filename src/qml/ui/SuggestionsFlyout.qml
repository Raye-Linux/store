// SPDX-License-Identifier: GPL-3.0-or-later
// The list under the search box: a few apps and "See all results".
import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.raye.store

Item {
    id: flyout
    // a SearchSuggestions
    property var suggestions
    readonly property int rowHeight: 40

    visible: suggestions.shown
    height: panel.height

    Glow { anchors.fill: panel; radius: Theme.radiusCard; spread: 18; offsetY: 6; color: Qt.alpha(Theme.shadow, Theme.night ? 0.5 : 0.16) }
    Rectangle {
        id: panel
        width: parent.width
        height: list.height + 2 * Theme.s2
        radius: Theme.radiusCard
        color: Theme.flyout
        border.width: 1
        border.color: Theme.strokeSurface

        Column {
            id: list
            x: Theme.s2
            y: Theme.s2
            width: parent.width - 2 * Theme.s2

            Repeater {
                model: flyout.suggestions.rows
                delegate: Row {
                    id: suggestion
                    required property var model
                    required property int index
                    selected: flyout.suggestions.current === index
                    onClicked: {
                        flyout.suggestions.current = index
                        flyout.suggestions.accept()
                    }
                    onHoveredChanged: if (hovered) flyout.suggestions.current = index
                    Kirigami.Icon {
                        Layout.leftMargin: Theme.s3
                        Layout.preferredWidth: 24
                        Layout.preferredHeight: 24
                        source: suggestion.model.iconSource || "application-x-executable"
                        fallback: "application-x-executable"
                    }
                    Text {
                        Layout.fillWidth: true
                        text: suggestion.model.name
                        font: Theme.body
                        color: Theme.text
                        elide: Text.ElideRight
                    }
                    Text {
                        Layout.rightMargin: Theme.s3
                        text: Format.categoryName(suggestion.model.categories)
                        font: Theme.caption
                        color: Theme.textTertiary
                    }
                }
            }
            Row {
                selected: flyout.suggestions.current === flyout.suggestions.count
                onClicked: {
                    flyout.suggestions.current = flyout.suggestions.count
                    flyout.suggestions.accept()
                }
                onHoveredChanged: if (hovered) flyout.suggestions.current = flyout.suggestions.count
                Glyph {
                    Layout.leftMargin: Theme.s3 + 4
                    name: "search"
                    size: 16
                    color: Theme.accentText
                }
                Text {
                    Layout.leftMargin: 4
                    Layout.fillWidth: true
                    text: i18nc("@action:button %1 is what was typed", "See all results for “%1”", flyout.suggestions.query)
                    font: Theme.body
                    color: Theme.accentText
                    elide: Text.ElideRight
                }
            }
        }
    }

    component Row: Rectangle {
        id: row
        default property alias content: rowLayout.data
        property bool selected: false
        readonly property alias hovered: rowMouse.containsMouse
        signal clicked()
        width: list.width
        height: flyout.rowHeight
        radius: Theme.radiusControl
        color: selected ? Theme.cardSolidHover : "transparent"
        Accessible.role: Accessible.ListItem
        MouseArea {
            id: rowMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: row.clicked()
        }
        RowLayout {
            id: rowLayout
            anchors.fill: parent
            spacing: Theme.s3
        }
    }
}
