// SPDX-License-Identifier: GPL-3.0-or-later
// One of the Store's own icons (icons/<name>.svg), drawn in a colour.
// They share one grid: 24 px, 1.5 px rounded strokes, and a filled
// twin ("home-filled") for the place you are on.
import QtQuick
import org.kde.kirigami as Kirigami

Kirigami.Icon {
    property string name
    property real size: 16

    implicitWidth: size
    implicitHeight: size
    source: name !== "" ? Qt.resolvedUrl("../icons/" + name + ".svg") : ""
    isMask: true
    color: Theme.text
    Accessible.ignored: true
}
