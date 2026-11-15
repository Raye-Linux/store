// SPDX-License-Identifier: GPL-3.0-or-later
// A rounded filter button: a category, "Apps" or "Games", a sort order.
import QtQuick
import QtQuick.Templates as T

T.AbstractButton {
    id: chip

    property bool selected: false

    implicitWidth: label.implicitWidth + 2 * Theme.s5
    implicitHeight: 32
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    Accessible.role: Accessible.Button
    Accessible.name: text
    Accessible.checked: selected
    Keys.onReturnPressed: clicked()
    Keys.onEnterPressed: clicked()

    background: Item {
        Rectangle {
            anchors.fill: parent
            radius: height / 2
            color: chip.selected ? (chip.hovered ? Theme.accentHover : Theme.accent)
                 : chip.down ? Theme.controlPressed : chip.hovered ? Theme.controlHover : Theme.control
            border.width: chip.selected ? 0 : 1
            border.color: Theme.strokeControl
            Behavior on color { ColorAnimation { duration: Theme.durationFaster } }
        }
        FocusRing { shown: chip.visualFocus; radius: height / 2 }
    }
    contentItem: Text {
        id: label
        text: chip.text
        font: chip.selected ? Theme.bodyStrong : Theme.body
        color: chip.selected ? Theme.textOnAccent : Theme.text
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
}
