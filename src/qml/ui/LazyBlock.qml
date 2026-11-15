// SPDX-License-Identifier: GPL-3.0-or-later
// A block of a page made only when the page scrolls near it, like a
// SectionRow: tiles of categories or collections.
import QtQuick
import QtQuick.Layouts

Item {
    id: block

    property Flickable page
    property real placeholderHeight: 200
    property alias sourceComponent: loader.sourceComponent
    readonly property Item item: loader.item

    property bool made: false
    readonly property bool near: page && y + page.topPadding < page.contentY + 2 * page.height
    onNearChanged: if (near) made = true
    Component.onCompleted: if (near) made = true

    Layout.fillWidth: true
    implicitHeight: made && loader.item ? loader.item.implicitHeight : placeholderHeight

    Loader {
        id: loader
        width: parent.width
        active: block.made
    }
}
