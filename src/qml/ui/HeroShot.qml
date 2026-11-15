// SPDX-License-Identifier: GPL-3.0-or-later
// A screenshot of the carousel, framed and shadowed. Its picture and
// size never change: the carousel makes a new one instead.
import QtQuick
import QtQuick.Window
import org.kde.kirigami as Kirigami

Kirigami.ShadowedTexture {
    id: shot

    property url url
    property int decodeHeight: 0
    property color borderColor: "#26ffffff"
    property real shadowSize: 36
    property real shadowOffset: 10
    property color shadowColor: "#73000000"
    property real shownOpacity: 1
    readonly property bool ready: picture.status === Image.Ready && picture.implicitHeight > 0
    readonly property real ratio: ready ? picture.implicitWidth / picture.implicitHeight : 16 / 10

    anchors.fill: parent
    radius: Theme.radiusCard + 2
    color: "#141318"
    border.width: 1
    border.color: borderColor
    shadow.size: shadowSize
    shadow.yOffset: shadowOffset
    shadow.color: shadowColor
    opacity: ready ? shownOpacity : 0
    Behavior on opacity { NumberAnimation { duration: Theme.durationNormal } }
    source: Image {
        id: picture
        source: shot.url
        asynchronous: true
        cache: false
        // decoded at the height it is shown
        sourceSize.height: Math.max(1, Math.ceil(shot.decodeHeight * Screen.devicePixelRatio))
        visible: false
    }
}
