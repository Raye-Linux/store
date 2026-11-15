// SPDX-License-Identifier: GPL-3.0-or-later
// A category or a collection to browse: its name, maybe a line about
// it, and the icons of three of its apps.
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import org.kde.kirigami as Kirigami
import org.raye.store.data

T.AbstractButton {
    id: tile

    // { kind, category, ids }: which apps, as for a SectionRow
    property var spec: ({})
    property string subtitle
    // a collection counts all its apps, to be hidden when too few exist
    readonly property int count: apps.count

    CatalogFilter {
        id: apps
        kind: tile.spec.kind || ""
        category: tile.spec.category || ""
        ids: tile.spec.ids || []
        limit: tile.spec.ids ? 0 : 3
        Component.onCompleted: sourceModel = catalog
    }

    implicitHeight: 84
    implicitWidth: 240
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    Accessible.role: Accessible.Button
    Accessible.name: text
    Keys.onReturnPressed: clicked()
    Keys.onEnterPressed: clicked()
    scale: down ? 0.98 : 1
    Behavior on scale { NumberAnimation { duration: Theme.durationFaster } }

    background: Item {
        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusCard
            color: tile.hovered ? Theme.cardSolidHover : Theme.cardSolid
            border.width: 1
            border.color: Theme.strokeSurface
            Behavior on color { ColorAnimation { duration: Theme.durationFast } }
        }
        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusCard
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0; color: "transparent" }
                GradientStop { position: 1; color: Qt.alpha(Theme.accent, Theme.night ? 0.16 : 0.08) }
            }
        }
        FocusRing { shown: tile.visualFocus; radius: Theme.radiusCard }
    }

    contentItem: RowLayout {
        spacing: Theme.s4
        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.s5
            spacing: 2
            Text {
                Layout.fillWidth: true
                text: tile.text
                font: Theme.tileTitle
                color: Theme.text
                wrapMode: Text.Wrap
                maximumLineCount: 2
                elide: Text.ElideRight
            }
            Text {
                Layout.fillWidth: true
                visible: tile.subtitle !== ""
                text: tile.subtitle
                font: Theme.caption
                color: Theme.textSecondary
                elide: Text.ElideRight
            }
        }
        Row {
            Layout.rightMargin: Theme.s5
            spacing: Theme.s2
            Repeater {
                model: apps
                delegate: Kirigami.Icon {
                    required property int index
                    required property var model
                    visible: index < 3
                    width: visible ? 32 : 0
                    height: 32
                    source: model.iconSource || "application-x-executable"
                    fallback: "application-x-executable"
                    // the icons rise one after the other under the pointer
                    transform: Translate {
                        y: tile.hovered ? -2 : 0
                        Behavior on y { NumberAnimation { duration: Theme.durationFast + index * 40; easing.type: Theme.easingFast } }
                    }
                }
            }
        }
    }
}
