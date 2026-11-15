// SPDX-License-Identifier: GPL-3.0-or-later
// A placeholder shown where something is still loading (a row of
// apps, a screenshot): a quiet block with a light passing over it, in
// place of a spinner. Still when animations are off.
import QtQuick

Rectangle {
    id: shimmer

    implicitWidth: 160
    implicitHeight: 16
    radius: 4
    color: Theme.control
    clip: true
    Accessible.role: Accessible.Indicator
    Accessible.name: i18nc("@info:status something is loading", "Loading")

    Rectangle {
        id: light
        width: Math.max(60, shimmer.width * 0.35)
        height: shimmer.height
        x: -width
        visible: !Theme.animationsOff
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0; color: "transparent" }
            GradientStop { position: 0.5; color: Theme.night ? "#12ffffff" : Qt.alpha(Theme.text, 0.05) }
            GradientStop { position: 1; color: "transparent" }
        }
        NumberAnimation on x {
            running: shimmer.visible && !Theme.animationsOff
            from: -light.width
            to: shimmer.width
            duration: 1500
            loops: Animation.Infinite
            easing.type: Easing.InOutSine
        }
    }
}
