// SPDX-License-Identifier: GPL-3.0-or-later
// A small menu of actions (the "..." of a Library row), in the Store's
// own colours.
import QtQuick
import QtQuick.Templates as T

T.Menu {
    id: menu

    implicitWidth: Math.max(180, contentItem.implicitWidth + leftPadding + rightPadding)
    implicitHeight: contentItem.implicitHeight + topPadding + bottomPadding
    padding: Theme.s2
    margins: Theme.s3

    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.durationFast }
        NumberAnimation { property: "y"; from: menu.y - 6; to: menu.y; duration: Theme.durationNormal; easing.type: Theme.easingEnter }
    }
    exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.durationFaster } }

    delegate: T.MenuItem {
        id: item
        implicitWidth: label.implicitWidth + 2 * Theme.s4
        implicitHeight: 34
        hoverEnabled: true
        background: Rectangle {
            radius: Theme.radiusControl
            color: item.down ? Theme.subtlePressed : item.highlighted || item.hovered ? Theme.subtleHover : "transparent"
        }
        contentItem: Text {
            id: label
            leftPadding: Theme.s4 - Theme.s2
            text: item.text
            font: Theme.body
            color: Theme.text
            verticalAlignment: Text.AlignVCenter
        }
    }

    contentItem: ListView {
        implicitHeight: contentHeight
        implicitWidth: 180
        model: menu.contentModel
        interactive: false
        currentIndex: menu.currentIndex
    }

    background: Item {
        Glow { radius: Theme.radiusCard; spread: 14; offsetY: 4; color: Qt.alpha(Theme.shadow, Theme.night ? 0.45 : 0.14) }
        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusCard
            color: Theme.flyout
            border.width: 1
            border.color: Theme.strokeSurface
        }
    }
}
