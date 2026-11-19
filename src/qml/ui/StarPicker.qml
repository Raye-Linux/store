// SPDX-License-Identifier: GPL-3.0-or-later
// Five stars to click for a rating. The pointer shows what a click
// would give; the arrow keys change it too.
import QtQuick
import QtQuick.Layouts

FocusScope {
    id: picker

    property int value: 0
    property real size: 32
    readonly property int shown: hover.hovered && hoverStars > 0 ? hoverStars : value
    property int hoverStars: 0

    implicitWidth: row.implicitWidth
    implicitHeight: row.implicitHeight
    activeFocusOnTab: true
    Accessible.role: Accessible.Slider
    Accessible.name: i18nc("@label", "Rating")
    Accessible.description: value > 0 ? i18ncp("@info", "One star", "%1 stars", value) : i18nc("@info", "No stars yet")

    Keys.onLeftPressed: value = Math.max(1, value - 1)
    Keys.onRightPressed: value = Math.min(5, value + 1)
    Keys.onPressed: event => {
        if (event.key >= Qt.Key_1 && event.key <= Qt.Key_5) {
            value = event.key - Qt.Key_0
            event.accepted = true
        }
    }

    HoverHandler { id: hover }

    Row {
        id: row
        spacing: Theme.s2
        Repeater {
            model: 5
            delegate: Item {
                id: star
                required property int index
                readonly property bool lit: index < picker.shown
                width: picker.size + Theme.s2
                height: picker.size + Theme.s2
                Glyph {
                    anchors.centerIn: parent
                    name: star.lit ? "star" : "star-outline"
                    size: picker.size
                    color: star.lit ? Theme.accentText : Theme.textTertiary
                    scale: star.lit && hover.hovered && picker.hoverStars === star.index + 1 ? 1.12 : 1
                    Behavior on scale { NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easingFast } }
                }
                HoverHandler {
                    cursorShape: Qt.PointingHandCursor
                    onHoveredChanged: if (hovered) picker.hoverStars = star.index + 1
                }
                TapHandler {
                    onTapped: picker.value = star.index + 1
                }
            }
        }
    }
    // for the keyboard only, as everywhere else
    FocusRing {
        shown: picker.activeFocus && (picker.focusReason === Qt.TabFocusReason || picker.focusReason === Qt.BacktabFocusReason)
        radius: Theme.radiusControl
    }
}
