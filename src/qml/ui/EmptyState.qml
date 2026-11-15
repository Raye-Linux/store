// SPDX-License-Identifier: GPL-3.0-or-later
// What a page shows when there is nothing to show: a glyph in a soft
// circle, a title, one line, and maybe an action, centred.
import QtQuick
import QtQuick.Layouts

ColumnLayout {
    id: empty

    property string glyph: "eye"
    property string title
    property string message
    default property alias actions: actionRow.data

    spacing: Theme.s3
    Accessible.role: Accessible.StaticText
    Accessible.name: title
    Accessible.description: message

    Rectangle {
        Layout.alignment: Qt.AlignHCenter
        Layout.bottomMargin: Theme.s4
        width: 88; height: 88; radius: 44
        color: Theme.control
        border.width: 1
        border.color: Theme.strokeSurface
        Glyph {
            anchors.centerIn: parent
            name: empty.glyph
            size: 40
            color: Theme.textSecondary
        }
    }
    Text {
        Layout.fillWidth: true
        text: empty.title
        visible: text !== ""
        font: Theme.subtitle
        color: Theme.text
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.Wrap
    }
    Text {
        Layout.fillWidth: true
        Layout.maximumWidth: 460
        Layout.alignment: Qt.AlignHCenter
        text: empty.message
        visible: text !== ""
        font: Theme.body
        color: Theme.textSecondary
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.Wrap
        lineHeightMode: Text.FixedHeight
        lineHeight: Theme.bodyLineHeight
    }
    Row {
        id: actionRow
        spacing: Theme.s3
        Layout.alignment: Qt.AlignHCenter
        Layout.topMargin: children.length > 0 ? Theme.s4 : 0
    }
}
