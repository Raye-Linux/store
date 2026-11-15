// SPDX-License-Identifier: GPL-3.0-or-later
// The title over a row or a block of a page, with "See all" at the
// right when there is more.
import QtQuick
import QtQuick.Layouts

RowLayout {
    id: header

    property string title
    property bool showAll: false
    signal seeAll()

    spacing: Theme.s3
    Accessible.role: Accessible.Heading
    Accessible.name: title

    Text {
        Layout.fillWidth: true
        text: header.title
        font: Theme.subtitle
        color: Theme.text
        elide: Text.ElideRight
        Accessible.ignored: true
    }
    Button {
        visible: header.showAll
        subtle: true
        // the words line up with the right edge of the row
        Layout.rightMargin: -Theme.s4
        text: i18nc("@action:button more apps of this row", "See all")
        textColor: Theme.accentText
        onClicked: header.seeAll()
    }
}
