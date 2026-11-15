// SPDX-License-Identifier: GPL-3.0-or-later
// A titled grid of category or collection tiles. A tile whose rule finds
// fewer apps than `minimum` is left out.
import QtQuick
import QtQuick.Layouts

ColumnLayout {
    id: block

    property string title
    // { name, subtitle, kind, category, ids }
    property var tiles: []
    property int minimum: 1
    signal show(var spec)

    spacing: Theme.s4

    SectionHeader { Layout.fillWidth: true; title: block.title }
    GridLayout {
        Layout.fillWidth: true
        columns: Math.max(1, Math.floor((block.width + Theme.rowSpacing) / (260 + Theme.rowSpacing)))
        columnSpacing: Theme.rowSpacing
        rowSpacing: Theme.rowSpacing
        Repeater {
            model: block.tiles
            delegate: CategoryTile {
                required property var modelData
                Layout.fillWidth: true
                visible: count >= block.minimum
                text: modelData.name
                subtitle: modelData.subtitle || ""
                spec: modelData
                onClicked: block.show({ title: modelData.name, kind: modelData.kind || "", category: modelData.category || "",
                                        ids: modelData.ids || [] })
            }
        }
    }
}
