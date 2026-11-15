// SPDX-License-Identifier: GPL-3.0-or-later
// A question that needs an answer before going on: a title, one line,
// the action and Cancel. The page behind is dimmed.
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import QtQuick.Templates as T

T.Popup {
    id: dialog

    property string title
    property string message
    property string actionText
    signal accepted()

    parent: QQC2.Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(440, parent ? parent.width - 48 : 440)
    implicitHeight: implicitContentHeight + topPadding + bottomPadding
    modal: true
    dim: true
    focus: true
    padding: 0
    closePolicy: T.Popup.CloseOnEscape | T.Popup.CloseOnPressOutside

    QQC2.Overlay.modal: Rectangle {
        color: Theme.smoke
        Behavior on opacity { NumberAnimation { duration: Theme.durationFast } }
    }

    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.durationFast }
        NumberAnimation { property: "scale"; from: 1.04; to: 1; duration: Theme.durationNormal; easing.type: Theme.easingEnter }
    }
    exit: Transition {
        NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.durationFaster }
    }

    background: Item {
        Glow { radius: Theme.radiusCard; spread: 24; offsetY: 8; color: Qt.alpha(Theme.shadow, Theme.night ? 0.55 : 0.18) }
        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusCard
            color: Theme.flyout
            border.width: 1
            border.color: Theme.strokeSurface
        }
        // the footer band, Windows' dialog style
        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 1
            height: footer.height
            radius: Theme.radiusCard
            color: Theme.flyoutFooter
            Rectangle { width: parent.width; height: Theme.radiusCard; color: parent.color }
        }
    }

    contentItem: ColumnLayout {
        spacing: 0
        ColumnLayout {
            Layout.fillWidth: true
            Layout.margins: Theme.s7
            spacing: Theme.s4
            Text {
                Layout.fillWidth: true
                text: dialog.title
                font: Theme.subtitle
                color: Theme.text
                wrapMode: Text.Wrap
            }
            Text {
                Layout.fillWidth: true
                text: dialog.message
                font: Theme.body
                color: Theme.textSecondary
                wrapMode: Text.Wrap
                lineHeightMode: Text.FixedHeight
                lineHeight: Theme.bodyLineHeight
            }
        }
        Item {
            id: footer
            Layout.fillWidth: true
            implicitHeight: 80
            RowLayout {
                anchors.fill: parent
                anchors.margins: Theme.s7
                spacing: Theme.s3
                Button {
                    Layout.fillWidth: true
                    accent: true
                    text: dialog.actionText
                    onClicked: {
                        dialog.accepted()
                        dialog.close()
                    }
                }
                Button {
                    id: cancelButton
                    Layout.fillWidth: true
                    text: i18n("Cancel")
                    onClicked: dialog.close()
                }
            }
        }
    }
    onOpened: cancelButton.forceActiveFocus()
}
