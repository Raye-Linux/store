// SPDX-License-Identifier: GPL-3.0-or-later
// A row of the Library: the app's icon, its name and a line under it,
// and whatever goes at the right (Open, Update, a progress bar).
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import org.kde.kirigami as Kirigami

T.AbstractButton {
    id: row

    property string iconSource
    property string title
    property string subtitle
    default property alias trailing: trailingRow.data
    // the right side grows (a progress bar) instead of sitting at the end
    property bool wideTrailing: false

    implicitHeight: Theme.listRowHeight
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    Accessible.role: Accessible.ListItem
    Accessible.name: title
    Accessible.description: subtitle

    background: Item {
        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusControl
            color: row.down ? Theme.cardPressed : row.hovered ? Theme.cardSolidHover : Theme.cardSolid
            border.width: 1
            border.color: Theme.strokeSurface
            Behavior on color { ColorAnimation { duration: Theme.durationFaster } }
        }
        FocusRing { shown: row.visualFocus }
    }

    contentItem: RowLayout {
        spacing: Theme.s4
        Kirigami.Icon {
            Layout.leftMargin: Theme.s5
            Layout.preferredWidth: 36
            Layout.preferredHeight: 36
            source: row.iconSource || "application-x-executable"
            fallback: "application-x-executable"
        }
        ColumnLayout {
            Layout.fillWidth: !row.wideTrailing
            Layout.preferredWidth: row.wideTrailing ? 200 : -1
            spacing: 0
            Text {
                Layout.fillWidth: true
                text: row.title
                font: Theme.bodyStrong
                color: Theme.text
                elide: Text.ElideRight
            }
            Text {
                Layout.fillWidth: true
                text: row.subtitle
                visible: text !== ""
                font: Theme.caption
                color: Theme.textSecondary
                elide: Text.ElideRight
            }
        }
        RowLayout {
            id: trailingRow
            Layout.fillWidth: row.wideTrailing
            Layout.rightMargin: Theme.s4
            spacing: Theme.s3
        }
    }
}
