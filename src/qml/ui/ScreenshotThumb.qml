// SPDX-License-Identifier: GPL-3.0-or-later
// A screenshot in the strip of an app's page: rounded, with a grey
// stand-in while it downloads. Opens the viewer.
import QtQuick
import QtQuick.Window
import QtQuick.Templates as T
import org.kde.kirigami as Kirigami

T.AbstractButton {
    id: thumb

    property url source
    property string caption

    // as wide as the picture is at this height; 16:10 until it is known
    property Item shown: null
    readonly property bool ready: shown !== null && shown.picture.status === Image.Ready && shown.picture.implicitHeight > 0
    readonly property real ratio: ready ? Math.min(2.2, Math.max(0.6, shown.picture.implicitWidth / shown.picture.implicitHeight)) : 16 / 10
    implicitWidth: height * ratio
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    Accessible.role: Accessible.Button
    Accessible.name: caption !== "" ? caption : i18nc("@action:button", "Show the screenshot")
    Keys.onReturnPressed: clicked()
    Keys.onEnterPressed: clicked()

    Rectangle {
        anchors.fill: parent
        visible: !thumb.ready
        radius: Theme.radiusCard
        color: Theme.cardSolid
        Shimmer { anchors.fill: parent; radius: Theme.radiusCard; color: "transparent" }
    }
    // a new frame for a new size instead of swapping the texture of the
    // one on screen (see HeroShot)
    Repeater {
        model: thumb.source.toString() !== "" && thumb.height > 0 ? [{ url: thumb.source, height: Math.ceil(thumb.height / 40) * 40 }] : []
        onItemAdded: (index, item) => thumb.shown = item
        delegate: Kirigami.ShadowedTexture {
            required property var modelData
            readonly property alias picture: image
            anchors.fill: parent
            visible: image.status === Image.Ready
            radius: Theme.radiusCard
            color: "transparent"
            border.width: 1
            border.color: Theme.strokeSurface
            source: Image {
                id: image
                source: modelData.url
                asynchronous: true
                cache: false
                // decoded at the size it is shown, not the full picture
                sourceSize.height: Math.ceil(modelData.height * Screen.devicePixelRatio)
                visible: false
            }
        }
    }
    Rectangle {
        anchors.fill: parent
        radius: Theme.radiusCard
        color: thumb.down ? "#1a000000" : thumb.hovered ? "#14ffffff" : "transparent"
        Behavior on color { ColorAnimation { duration: Theme.durationFaster } }
    }
    FocusRing { shown: thumb.visualFocus; radius: Theme.radiusCard }
}
