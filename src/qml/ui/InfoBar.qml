// SPDX-License-Identifier: GPL-3.0-or-later
// A message over a page: a coloured mark, the words, maybe an action,
// and a close button. Used when something did not work.
import QtQuick
import QtQuick.Layouts

Rectangle {
    id: bar

    // "error", "warning" or "info"
    property string severity: "error"
    property string text
    property bool closable: true
    default property alias actions: actionRow.data
    signal closed()

    readonly property color tint: severity === "error" ? Theme.critical
                                : severity === "warning" ? Theme.caution : Theme.info

    implicitHeight: Math.max(48, row.implicitHeight + 2 * Theme.s4)
    radius: Theme.radiusControl
    color: Theme.mix(Theme.cardSolid, tint, Theme.night ? 0.14 : 0.1)
    border.width: 1
    border.color: Qt.alpha(tint, 0.35)
    Accessible.role: Accessible.AlertMessage
    Accessible.name: text

    RowLayout {
        id: row
        anchors.fill: parent
        anchors.leftMargin: Theme.s5
        anchors.rightMargin: Theme.s3
        spacing: Theme.s4
        Rectangle {
            Layout.alignment: Qt.AlignVCenter
            width: 16; height: 16; radius: 8
            color: bar.tint
            Text {
                anchors.centerIn: parent
                text: bar.severity === "info" ? "i" : "!"
                font.family: "Inter"
                font.pixelSize: 11
                font.weight: Font.Bold
                color: Theme.night ? Theme.backdropSolid : "#ffffff"
            }
        }
        Text {
            Layout.fillWidth: true
            text: bar.text
            font: Theme.body
            color: Theme.text
            wrapMode: Text.Wrap
        }
        Row { id: actionRow; spacing: Theme.s3; Layout.alignment: Qt.AlignVCenter }
        Button {
            visible: bar.closable
            subtle: true
            glyph: "close"
            glyphSize: 12
            implicitWidth: 32
            Accessible.name: i18nc("@action:button close the message", "Close")
            onClicked: bar.closed()
        }
    }
}
