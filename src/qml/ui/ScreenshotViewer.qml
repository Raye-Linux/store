// SPDX-License-Identifier: GPL-3.0-or-later
// A screenshot as large as the window allows, over a dark veil, with
// arrows and "2 of 5". Esc or a click on the veil closes it.
import QtQuick
import QtQuick.Window
import QtQuick.Controls as QQC2
import QtQuick.Templates as T

T.Popup {
    id: viewer

    property var shots: []
    property int index: 0
    function show(i) { index = i; open() }
    function step(by) { if (shots.length > 0) index = (index + by + shots.length) % shots.length }

    parent: QQC2.Overlay.overlay
    x: 0
    y: 0
    width: parent ? parent.width : 0
    height: parent ? parent.height : 0
    modal: true
    focus: true
    padding: 0
    closePolicy: T.Popup.CloseOnEscape

    enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.durationFast } }
    exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.durationFaster } }

    background: Rectangle { color: "#e6050309" }

    contentItem: FocusScope {
        focus: true
        Keys.onLeftPressed: viewer.step(-1)
        Keys.onRightPressed: viewer.step(1)

        TapHandler { onTapped: viewer.close() }

        Image {
            id: big
            anchors.fill: parent
            anchors.margins: 72
            anchors.bottomMargin: 96
            source: viewer.shots.length > 0 ? (viewer.shots[viewer.index].full || viewer.shots[viewer.index].thumb) : ""
            fillMode: Image.PreserveAspectFit
            asynchronous: true
            cache: false
            // in 80 px steps: not decoded again on every pixel of a resize
            sourceSize.width: Math.ceil(Math.ceil(width / 80) * 80 * Screen.devicePixelRatio)
            sourceSize.height: Math.ceil(Math.ceil(height / 80) * 80 * Screen.devicePixelRatio)
            // a click on the picture keeps it open
            TapHandler { }
            scale: status === Image.Ready ? 1 : 0.97
            opacity: status === Image.Ready ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: Theme.durationFast } }
            Behavior on scale { NumberAnimation { duration: Theme.durationNormal; easing.type: Theme.easingEnter } }
        }
        Shimmer {
            visible: big.status !== Image.Ready
            anchors.centerIn: parent
            width: 320; height: 6; radius: 3
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 36
            text: viewer.shots.length > 1 ? i18nc("@info screenshot 2 of 5", "%1 of %2", viewer.index + 1, viewer.shots.length) : ""
            font: Theme.body
            color: "#ccffffff"
        }
        RoundArrow {
            glyph: "chevron-left"
            anchors.left: parent.left
            anchors.leftMargin: Theme.s6
            anchors.verticalCenter: parent.verticalCenter
            shown: viewer.shots.length > 1
            Accessible.name: i18nc("@action:button screenshots", "Previous")
            onClicked: viewer.step(-1)
        }
        RoundArrow {
            glyph: "chevron-right"
            anchors.right: parent.right
            anchors.rightMargin: Theme.s6
            anchors.verticalCenter: parent.verticalCenter
            shown: viewer.shots.length > 1
            Accessible.name: i18nc("@action:button screenshots", "Next")
            onClicked: viewer.step(1)
        }
        RoundArrow {
            glyph: "close"
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: Theme.s6
            Accessible.name: i18nc("@action:button", "Close")
            onClicked: viewer.close()
        }
    }
}
