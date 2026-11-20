// SPDX-License-Identifier: GPL-3.0-or-later
// A place in the navigation rail: its icon over a short name. The
// place you are on has its icon filled; the accent pill beside it is
// drawn by the rail, so it can slide from one place to the next.
import QtQuick
import QtQuick.Templates as T
import QtQuick.Controls as QQC2

T.AbstractButton {
    id: item

    // the glyph's name; "<glyph>-filled" is shown when selected
    property string glyph
    property bool selected: false
    // a small mark at the top right of the icon: a number, or a
    // download arrow while something installs
    property int badgeCount: 0
    property bool busy: false
    // the key that comes here ("Ctrl+1"), shown in the tooltip
    property string shortcut

    QQC2.ToolTip.visible: hovered && shortcut !== ""
    QQC2.ToolTip.delay: 600
    QQC2.ToolTip.text: i18nc("@info:tooltip a place and its keyboard shortcut", "%1 (%2)", text, shortcut)

    implicitWidth: Theme.railItemWidth
    implicitHeight: Theme.railItemHeight
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    Accessible.role: Accessible.PageTab
    Accessible.name: text
    Accessible.selected: selected
    Keys.onReturnPressed: clicked()
    Keys.onEnterPressed: clicked()

    background: Item {
        Rectangle {
            anchors.fill: parent
            anchors.margins: 2
            radius: Theme.radiusControl
            color: item.selected ? (item.hovered ? Theme.controlHover : Theme.control)
                 : item.down ? Theme.subtlePressed
                 : item.hovered ? Theme.subtleHover : "transparent"
            Behavior on color { ColorAnimation { duration: Theme.durationFaster } }
        }
        FocusRing { shown: item.visualFocus; anchors.margins: 2 - Theme.focusRingOffset }
    }

    contentItem: Item {
        Item {
            id: iconBox
            width: Theme.railIcon
            height: Theme.railIcon
            anchors.horizontalCenter: parent.horizontalCenter
            y: 9
            // pressed, the icon gives a little
            scale: item.down ? 0.9 : 1
            Behavior on scale { NumberAnimation { duration: Theme.durationFaster } }

            Glyph {
                anchors.fill: parent
                name: item.glyph
                size: Theme.railIcon
                color: Theme.textSecondary
                opacity: item.selected ? 0 : 1
                Behavior on opacity { NumberAnimation { duration: Theme.durationFast } }
            }
            Glyph {
                anchors.fill: parent
                name: item.glyph + "-filled"
                size: Theme.railIcon
                color: Theme.text
                opacity: item.selected ? 1 : 0
                Behavior on opacity { NumberAnimation { duration: Theme.durationFast } }
            }

            Rectangle {
                id: badge
                visible: item.busy || item.badgeCount > 0
                height: 16
                width: item.busy ? 16 : Math.max(16, count.implicitWidth + 8)
                radius: 8
                x: parent.width - 7
                y: -5
                color: Theme.accent
                border.width: 2
                border.color: Theme.backdropSolid
                Text {
                    id: count
                    visible: !item.busy
                    anchors.centerIn: parent
                    text: item.badgeCount > 99 ? "99+" : item.badgeCount
                    font.family: "Inter"
                    font.pixelSize: 10
                    font.weight: Font.DemiBold
                    color: Theme.textOnAccent
                }
                Glyph {
                    id: arrow
                    visible: item.busy
                    anchors.horizontalCenter: parent.horizontalCenter
                    name: "download"
                    size: 10
                    color: Theme.textOnAccent
                    // falls gently, like something arriving
                    SequentialAnimation on y {
                        running: item.busy && !Theme.animationsOff
                        loops: Animation.Infinite
                        NumberAnimation { from: 1; to: 4; duration: 700; easing.type: Easing.InOutSine }
                        NumberAnimation { from: 4; to: 1; duration: 700; easing.type: Easing.InOutSine }
                    }
                }
            }
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: iconBox.bottom
            anchors.topMargin: 5
            width: parent.width - 4
            horizontalAlignment: Text.AlignHCenter
            text: item.text
            font.family: "Inter"
            font.pixelSize: 11
            font.weight: item.selected ? Font.DemiBold : Font.Medium
            color: item.selected ? Theme.text : Theme.textSecondary
            elide: Text.ElideRight
            Accessible.ignored: true
        }
    }
}
