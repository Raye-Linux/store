// SPDX-License-Identifier: GPL-3.0-or-later
// "Keyboard shortcuts": what the Store's keys do (F1 or Ctrl+/). The
// list is the same one Main.qml binds: tests/test_shortcuts.py checks
// that every key shown here does what it says.
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import QtQuick.Templates as T

T.Popup {
    id: dialog

    // what each key does. `keys`: the sequences Main.qml binds (the test
    // reads them); `shown`: how a person reads them, when not the same
    readonly property var shortcuts: [
        { keys: ["Ctrl+F", "/"], what: i18nc("@info keyboard shortcut", "Search") },
        { keys: ["Esc"], shown: i18nc("@info a key on the keyboard", "Esc"),
          what: i18nc("@info keyboard shortcut", "Clear the search, or close a window") },
        { keys: ["Alt+Left", "Backspace"], shown: i18nc("@info keys on the keyboard", "Alt+Left  /  Backspace"),
          what: i18nc("@info keyboard shortcut", "Go back") },
        { keys: ["Alt+Right"], shown: i18nc("@info keys on the keyboard", "Alt+Right"),
          what: i18nc("@info keyboard shortcut", "Go forward") },
        { keys: ["F5", "Ctrl+R"], what: i18nc("@info keyboard shortcut", "Refresh") },
        { keys: ["Ctrl+1"], what: i18nc("@info keyboard shortcut", "Home") },
        { keys: ["Ctrl+2"], what: i18nc("@info keyboard shortcut", "Apps") },
        { keys: ["Ctrl+3"], what: i18nc("@info keyboard shortcut", "Games") },
        { keys: ["Ctrl+4"], what: i18nc("@info keyboard shortcut", "Library") },
        { keys: ["Ctrl+,"], what: i18nc("@info keyboard shortcut", "Store settings") },
        { keys: ["Enter"], shown: i18nc("@info a key on the keyboard", "Enter"),
          what: i18nc("@info keyboard shortcut", "Open the selected app") },
        { keys: [], shown: i18nc("@info keys on the keyboard", "Arrows"),
          what: i18nc("@info keyboard shortcut", "Scroll, or move between apps") },
        { keys: [], shown: i18nc("@info keys on the keyboard", "Page Up  /  Page Down  /  Space"),
          what: i18nc("@info keyboard shortcut", "Scroll by a page") },
        { keys: [], shown: i18nc("@info keys on the keyboard", "Home  /  End"),
          what: i18nc("@info keyboard shortcut", "Go to the top or the bottom") },
        { keys: ["F1", "Ctrl+/"], what: i18nc("@info keyboard shortcut", "Show these shortcuts") },
        { keys: ["Ctrl+W", "Ctrl+Q"], what: i18nc("@info keyboard shortcut", "Close the Store") },
    ]

    parent: QQC2.Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(520, parent ? parent.width - 48 : 520)
    height: Math.min(implicitHeight, parent ? parent.height - 48 : implicitHeight)
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
    }

    contentItem: ColumnLayout {
        spacing: Theme.s4
        Accessible.role: Accessible.Dialog
        Accessible.name: title.text
        Text {
            id: title
            Layout.fillWidth: true
            Layout.topMargin: Theme.s7
            Layout.leftMargin: Theme.s7
            Layout.rightMargin: Theme.s7
            text: i18nc("@title:window", "Keyboard shortcuts")
            font: Theme.subtitle
            color: Theme.text
        }
        Flickable {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: Theme.s7
            Layout.rightMargin: Theme.s7
            implicitHeight: rows.implicitHeight
            contentHeight: rows.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            QQC2.ScrollBar.vertical: ThinScrollBar {}
            GridLayout {
                id: rows
                width: list.width
                columns: 2
                columnSpacing: Theme.s6
                rowSpacing: Theme.s2
                Repeater {
                    model: dialog.shortcuts
                    delegate: Text {
                        required property var modelData
                        required property int index
                        Layout.row: index
                        Layout.column: 0
                        text: modelData.shown !== undefined ? modelData.shown : modelData.keys.join("  /  ")
                        font: Theme.bodyStrong
                        color: Theme.text
                    }
                }
                Repeater {
                    model: dialog.shortcuts
                    delegate: Text {
                        required property var modelData
                        required property int index
                        Layout.row: index
                        Layout.column: 1
                        Layout.fillWidth: true
                        text: modelData.what
                        font: Theme.body
                        color: Theme.textSecondary
                        wrapMode: Text.Wrap
                    }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: Theme.s7
            Item { Layout.fillWidth: true }
            Button {
                id: closeButton
                text: i18nc("@action:button", "Close")
                onClicked: dialog.close()
            }
        }
    }
    onOpened: closeButton.forceActiveFocus()
}
