// SPDX-License-Identifier: GPL-3.0-or-later
// Steam and Heroic side by side, big, with their Install button: the
// way to the games people already own on Windows. A launcher the
// catalog doesn't have is left out, and so is the whole block when
// neither is there.
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import org.kde.kirigami as Kirigami

ColumnLayout {
    id: spotlight

    signal open(var app)

    // goes up when the catalog is read again
    property int revision: 0
    Connections {
        target: catalog
        function onLoadingChanged() { if (!catalog.loading) spotlight.revision++ }
    }
    readonly property var launchers: revision >= 0 ? [
        { app: catalog.find("com.valvesoftware.Steam"),
          line: i18nc("@info what Steam brings", "Your Steam library. Windows games run too: Steam Play is already on.") },
        { app: catalog.find("com.heroicgameslauncher.hgl"),
          line: i18nc("@info what Heroic brings", "Your Epic Games, GOG and Amazon games, in one launcher.") }
    ].filter(l => l.app.appId !== undefined && l.app.listed) : []

    visible: launchers.length > 0
    spacing: Theme.s4

    SectionHeader {
        Layout.fillWidth: true
        title: i18nc("@title", "Bring your PC games")
        showAll: false
    }

    GridLayout {
        Layout.fillWidth: true
        columns: spotlight.width >= 720 ? 2 : 1
        columnSpacing: Theme.rowSpacing
        rowSpacing: Theme.rowSpacing

        Repeater {
            model: spotlight.launchers
            delegate: T.AbstractButton {
                id: tile
                required property var modelData
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                implicitHeight: 112
                hoverEnabled: true
                focusPolicy: Qt.StrongFocus
                Accessible.role: Accessible.Button
                Accessible.name: modelData.app.name
                Accessible.description: modelData.line
                Keys.onReturnPressed: clicked()
                Keys.onEnterPressed: clicked()
                onClicked: spotlight.open(modelData.app)
                scale: down ? 0.99 : 1
                Behavior on scale { NumberAnimation { duration: Theme.durationFaster } }

                background: Rectangle {
                    radius: Theme.radiusCard
                    color: tile.hovered ? Theme.cardSolidHover : Theme.cardSolid
                    border.width: 1
                    border.color: Theme.strokeSurface
                    // the launcher's own colour, faint, from the left
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0; color: Qt.alpha(Format.brand(tile.modelData.app), Theme.night ? 0.3 : 0.16) }
                        GradientStop { position: 0.6; color: tile.hovered ? Theme.cardSolidHover : Theme.cardSolid }
                    }
                    Behavior on color { ColorAnimation { duration: Theme.durationFast } }
                    FocusRing { shown: tile.visualFocus; radius: Theme.radiusCard }
                }

                contentItem: RowLayout {
                    spacing: Theme.s5
                    Kirigami.Icon {
                        Layout.leftMargin: Theme.s6
                        Layout.preferredWidth: 64
                        Layout.preferredHeight: 64
                        source: tile.modelData.app.iconSource || "applications-games"
                        fallback: "applications-games"
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.s1
                        Text {
                            Layout.fillWidth: true
                            text: tile.modelData.app.name
                            font: Theme.subtitle
                            color: Theme.text
                            elide: Text.ElideRight
                        }
                        Text {
                            Layout.fillWidth: true
                            text: tile.modelData.line
                            font: Theme.body
                            color: Theme.textSecondary
                            wrapMode: Text.Wrap
                            maximumLineCount: 2
                            elide: Text.ElideRight
                        }
                    }
                    GetButton {
                        Layout.rightMargin: Theme.s6
                        app: tile.modelData.app
                    }
                }
            }
        }
    }
}
