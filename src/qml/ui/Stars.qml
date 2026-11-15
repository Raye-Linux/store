// SPDX-License-Identifier: GPL-3.0-or-later
// Five stars, filled as far as the average goes.
import QtQuick

Row {
    id: stars

    property real value: 0
    property real size: 14
    property color color: Theme.text

    spacing: 2
    Accessible.role: Accessible.StaticText
    Accessible.name: i18nc("@info rating", "%1 out of 5 stars", Format.rating(value))

    Repeater {
        model: 5
        // whole and half stars: a clip would not hold in software drawing
        delegate: Item {
            required property int index
            readonly property real part: stars.value - index
            width: stars.size
            height: stars.size
            Glyph { anchors.fill: parent; name: "star"; size: stars.size; color: Qt.alpha(stars.color, 0.22) }
            Glyph {
                anchors.fill: parent
                visible: part >= 0.25
                name: part >= 0.75 ? "star" : "star-half"
                size: stars.size
                color: stars.color
            }
        }
    }
}
