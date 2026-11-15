// SPDX-License-Identifier: GPL-3.0-or-later
// The page scroll bar: a 2 px line over the content while it scrolls,
// a 6 px thumb on a 12 px track when the pointer comes close.
import QtQuick
import QtQuick.Templates as T

T.ScrollBar {
    id: bar

    readonly property bool wide: hovered || pressed

    implicitWidth: orientation === Qt.Vertical ? 12 : 200
    implicitHeight: orientation === Qt.Vertical ? 200 : 12
    padding: 2
    hoverEnabled: true
    minimumSize: 0.08
    visible: size < 1 && policy !== T.ScrollBar.AlwaysOff
    Accessible.role: Accessible.ScrollBar

    contentItem: Rectangle {
        readonly property real thickness: bar.wide ? 6 : 2
        implicitWidth: bar.orientation === Qt.Vertical ? thickness : 100
        implicitHeight: bar.orientation === Qt.Vertical ? 100 : thickness
        radius: thickness / 2
        color: bar.wide ? Theme.textSecondary : Qt.alpha(Theme.textTertiary, 0.5)
        opacity: bar.active || bar.wide ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: bar.active ? Theme.durationFaster : Theme.durationSlow } }
        Behavior on implicitWidth { NumberAnimation { duration: Theme.durationFast } }
        Behavior on implicitHeight { NumberAnimation { duration: Theme.durationFast } }
    }

    background: Rectangle {
        radius: 6
        color: Qt.alpha(Theme.night ? "#1a191f" : "#fbfbfc", 0.7)
        opacity: bar.wide ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: Theme.durationFast } }
    }
}
