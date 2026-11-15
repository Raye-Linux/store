// SPDX-License-Identifier: GPL-3.0-or-later
// The round arrow on the side of a row or a carousel, there while the
// pointer is over it.
import QtQuick
import QtQuick.Templates as T

T.AbstractButton {
    id: arrow

    property string glyph: "chevron-right"
    property bool shown: true

    width: 36
    height: 36
    opacity: shown ? 1 : 0
    visible: opacity > 0
    enabled: shown
    hoverEnabled: true
    focusPolicy: Qt.NoFocus
    Accessible.role: Accessible.Button
    Behavior on opacity { NumberAnimation { duration: Theme.durationFast } }

    background: Item {
        Glow {
            radius: width / 2
            spread: 8
            offsetY: 2
            color: Qt.alpha(Theme.shadow, Theme.night ? 0.45 : 0.15)
        }
        Rectangle {
            anchors.fill: parent
            radius: width / 2
            color: arrow.down ? Theme.flyoutFooter : arrow.hovered ? Theme.cardSolidHover : Theme.flyout
            border.width: 1
            border.color: Theme.strokeSurface
            Behavior on color { ColorAnimation { duration: Theme.durationFaster } }
        }
    }
    contentItem: Item {
        Glyph {
            anchors.centerIn: parent
            name: arrow.glyph
            size: 16
        }
    }
}
