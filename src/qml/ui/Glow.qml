// SPDX-License-Identifier: GPL-3.0-or-later
// A soft light (or shadow) around a rounded shape: rings of the same
// colour fading out. Drawn with plain rectangles, so it looks the same
// on the GPU and in software rendering. Put it behind the shape.
import QtQuick

Item {
    id: glow

    // the corner radius of the shape it surrounds
    property real radius: Theme.radiusControl
    // how far the light reaches, in pixels
    property real spread: 12
    property color color: Theme.glow
    // a shadow falls a little lower than its shape
    property real offsetY: 0

    readonly property int _rings: 6
    readonly property real _step: spread / _rings

    anchors.fill: parent
    Accessible.ignored: true

    Repeater {
        model: glow.spread > 0 ? glow._rings : 0
        delegate: Rectangle {
            required property int index
            readonly property real reach: (index + 1) * glow._step
            x: -reach
            y: -reach + glow.offsetY
            width: glow.width + 2 * reach
            height: glow.height + 2 * reach
            radius: glow.radius + reach
            color: "transparent"
            border.width: glow._step
            // most of the light close to the shape, none at the far edge
            border.color: Qt.alpha(glow.color, glow.color.a * Math.pow(1 - index / glow._rings, 2) / 2.2)
        }
    }
}
