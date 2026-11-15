// SPDX-License-Identifier: GPL-3.0-or-later
// The keyboard focus ring: 2 px outer line and 1 px inner line outside
// the control, plus Raye's glow at night. It shows for the keyboard
// only (a control's visualFocus), never after a click.
import QtQuick

Item {
    id: ring

    property bool shown: false
    // the corner radius of the control
    property real radius: Theme.radiusControl

    anchors.fill: parent
    anchors.margins: -Theme.focusRingOffset
    visible: shown
    Accessible.ignored: true

    Glow {
        visible: Theme.night
        radius: ring.radius + Theme.focusRingOffset
        spread: 4
        color: Theme.glow
    }
    Rectangle {
        anchors.fill: parent
        radius: ring.radius + Theme.focusRingOffset
        color: "transparent"
        border.width: 2
        border.color: Theme.focusOuter
    }
    Rectangle {
        anchors.fill: parent
        anchors.margins: 2
        radius: ring.radius + 1
        color: "transparent"
        border.width: 1
        border.color: Theme.focusInner
    }
}
