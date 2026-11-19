// SPDX-License-Identifier: GPL-3.0-or-later
// One app in a row or a grid: its icon on a tile tinted with the app's
// own colour, the name, what kind of app it is, its rating and how many
// gave one, and Free, Installed or Preview. Lifts a little under the pointer.
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import org.kde.kirigami as Kirigami

T.AbstractButton {
    id: card

    // a catalog entry: a map from catalog.find(), or a row of a filter
    property var app: null
    property bool installed: false
    // in a row of Windows programs; elsewhere a native app shows its category
    property bool windowsRow: false
    readonly property string windowsLabel: !app || (app.windowsState === "native" && !windowsRow) ? "" : Format.windowsLabel(app)
    // in a row of apps that stand in for Windows programs, or in search
    // results: which program, instead of the category
    property bool insteadRow: false
    readonly property string insteadLabel: insteadRow ? Format.replacesText(app) : ""
    function refresh() { installed = app && app.ref ? catalog.isInstalled(app.ref) : false }
    onAppChanged: refresh()
    Connections {
        target: catalog
        function onInstalledChanged() { card.refresh() }
    }

    implicitWidth: Theme.cardWidth
    implicitHeight: Theme.cardHeight
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    Accessible.role: Accessible.Button
    Accessible.name: app ? app.name : ""
    Accessible.description: app ? app.summary : ""
    Keys.onReturnPressed: clicked()
    Keys.onEnterPressed: clicked()

    scale: down ? 0.98 : 1
    Behavior on scale { NumberAnimation { duration: Theme.durationFaster; easing.type: Theme.easingFast } }

    // at rest a card is flat (rows of shadows cost every frame while the
    // page scrolls); the one under the pointer lifts with a soft shadow
    background: Item {
        Kirigami.ShadowedRectangle {
            anchors.fill: parent
            visible: opacity > 0
            opacity: card.hovered ? 1 : 0
            radius: Theme.radiusCard
            color: "transparent"
            shadow.size: 18
            shadow.yOffset: 6
            shadow.color: Qt.alpha(Theme.shadow, Theme.night ? 0.5 : 0.14)
            Behavior on opacity { NumberAnimation { duration: Theme.durationFast } }
        }
        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusCard
            color: card.hovered ? Theme.cardSolidHover : Theme.cardSolid
            border.width: 1
            border.color: Theme.strokeSurface
            Behavior on color { ColorAnimation { duration: Theme.durationFast } }
        }
        FocusRing { shown: card.visualFocus; radius: Theme.radiusCard }
    }

    contentItem: ColumnLayout {
        spacing: 0

        // the tile: the app's colour, faint, with the icon on it
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.cardTile
            Layout.margins: Theme.s2
            Layout.bottomMargin: 0
            radius: Theme.radiusCard - 2
            // one flat tint: a gradient here would be painted again for every
            // card on every frame of a scroll
            color: Qt.alpha(Format.brand(card.app), Theme.night ? 0.22 : 0.12)
            Kirigami.Icon {
                anchors.centerIn: parent
                width: Theme.cardIcon
                height: Theme.cardIcon
                source: card.app ? (card.app.iconSource || "application-x-executable") : ""
                fallback: "application-x-executable"
                scale: card.hovered ? 1.04 : 1
                Behavior on scale { NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easingFast } }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: Theme.s4
            Layout.topMargin: Theme.s4 - 2
            spacing: 2

            Text {
                Layout.fillWidth: true
                text: card.app ? card.app.name : ""
                font: Theme.bodyStrong
                color: Theme.text
                wrapMode: Text.Wrap
                maximumLineCount: 2
                elide: Text.ElideRight
                lineHeightMode: Text.FixedHeight
                lineHeight: Theme.bodyLineHeight
            }
            // a Windows program says how it runs here instead of its category
            Text {
                Layout.fillWidth: true
                text: !card.app ? "" : card.windowsLabel || card.insteadLabel || Format.categoryName(card.app.categories)
                visible: text !== ""
                font: card.windowsLabel ? Theme.captionStrong : Theme.caption
                color: card.windowsLabel ? Format.windowsTint(card.app) : card.insteadLabel ? Theme.accentText : Theme.textSecondary
                // "Instead of Microsoft Office" needs the second line
                wrapMode: card.insteadLabel && !card.windowsLabel ? Text.Wrap : Text.NoWrap
                maximumLineCount: 2
                elide: Text.ElideRight
            }
            Item { Layout.fillHeight: true }
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.s1
                Glyph {
                    visible: Format.hasRating(card.app)
                    name: "star"
                    size: 11
                    color: Theme.text
                }
                Text {
                    visible: Format.hasRating(card.app)
                    text: visible ? Format.rating(card.app.ratingAvg) : ""
                    font: Theme.caption
                    color: Theme.text
                }
                Text {
                    visible: Format.hasRating(card.app)
                    text: visible ? i18nc("@info how many ratings, on an app card: (1.2K)", "(%1)", Format.count(card.app.ratingCount)) : ""
                    font: Theme.caption
                    color: Theme.textTertiary
                    Accessible.name: visible ? i18ncp("@info", "One rating", "%1 ratings", card.app.ratingCount) : ""
                }
                Item { Layout.fillWidth: true }
                Text {
                    // a Windows program's price is its maker's business
                    visible: card.installed || !card.app || card.app.source !== "windows"
                    text: card.installed ? i18nc("@info on an app card", "Installed")
                        : card.app && card.app.preview === true ? i18nc("@info on an app card: an early version", "Preview")
                        : i18nc("@info on an app card: costs nothing", "Free")
                    font: Theme.captionStrong
                    color: card.installed ? Theme.success
                         : card.app && card.app.preview === true ? Theme.caution : Theme.textSecondary
                }
            }
        }
    }
}
