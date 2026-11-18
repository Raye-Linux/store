// SPDX-License-Identifier: GPL-3.0-or-later
// A page of the Store: one column that scrolls, the page margins, and
// the thin scroll bar.
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2

Flickable {
    id: page
    WheelScroll { target: page }

    default property alias content: column.data
    property real padding: Theme.contentPadding
    property real topPadding: Theme.s7
    property real maxWidth: Theme.contentMaxWidth
    property alias spacing: column.spacing
    readonly property real columnWidth: column.width

    function toTop() { contentY = 0 }

    contentWidth: width
    contentHeight: column.implicitHeight + topPadding + padding
    clip: true
    boundsBehavior: Flickable.StopAtBounds
    flickableDirection: Flickable.VerticalFlick
    QQC2.ScrollBar.vertical: ThinScrollBar {}

    ColumnLayout {
        id: column
        width: Math.min(page.width - 2 * page.padding, page.maxWidth)
        x: (page.width - width) / 2
        y: page.topPadding
        spacing: Theme.sectionGap
    }
}
