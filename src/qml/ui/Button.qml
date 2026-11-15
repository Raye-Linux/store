// SPDX-License-Identifier: GPL-3.0-or-later
// A push button. `accent` is the one button of a place that says "do
// it" (Install, Open): the accent colour with its lit top edge.
// `subtle` has no fill until the pointer is on it (Back, See all).
import QtQuick
import QtQuick.Templates as T

T.Button {
    id: control

    property bool accent: false
    property bool subtle: false
    // one of the Store's glyphs, before the text
    property string glyph
    property real glyphSize: 16
    property color textColor: !enabled ? Theme.textDisabled
                            : accent ? Theme.textOnAccent
                            : down ? Theme.textSecondary : Theme.text

    readonly property bool showHover: enabled && hovered
    readonly property bool showPressed: enabled && down

    implicitWidth: Math.max(implicitContentWidth + leftPadding + rightPadding,
                            text === "" ? implicitHeight : 80)
    implicitHeight: Theme.controlHeight
    leftPadding: text === "" ? 0 : Theme.s4
    rightPadding: text === "" ? 0 : Theme.s4
    topPadding: 0
    bottomPadding: 0
    spacing: Theme.s3
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    font: Theme.body

    Accessible.role: Accessible.Button
    Accessible.name: text

    Keys.onReturnPressed: control.clicked()
    Keys.onEnterPressed: control.clicked()

    // the label stays centred when a layout makes the button wider
    contentItem: Item {
        implicitWidth: labelRow.implicitWidth
        implicitHeight: labelRow.implicitHeight
        Row {
            id: labelRow
            anchors.centerIn: parent
            spacing: control.spacing
            Glyph {
                visible: control.glyph !== ""
                anchors.verticalCenter: parent.verticalCenter
                name: control.glyph
                size: control.glyphSize
                color: control.textColor
            }
            Text {
                visible: text !== ""
                anchors.verticalCenter: parent.verticalCenter
                text: control.text
                font: control.font
                color: control.textColor
                Accessible.ignored: true
            }
        }
    }

    background: Item {
        implicitWidth: 80
        implicitHeight: Theme.controlHeight

        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusControl
            color: control.accent
                   ? (!control.enabled ? Theme.accentDisabled
                      : control.showPressed ? Theme.accentPressed
                      : control.showHover ? Theme.accentHover : Theme.accent)
                   : control.subtle
                     ? (control.showPressed ? Theme.subtlePressed
                        : control.showHover ? Theme.subtleHover : "transparent")
                     : (!control.enabled ? Theme.controlDisabled
                        : control.showPressed ? Theme.controlPressed
                        : control.showHover ? Theme.controlHover : Theme.control)
            border.width: control.accent || control.subtle ? 0 : 1
            border.color: Theme.strokeControl
            Behavior on color { ColorAnimation { duration: Theme.durationFaster } }
        }
        // the elevation edge: a darker bottom line on a plain button,
        // a lit top line on the accent one
        Item {
            width: parent.width
            height: 1
            y: control.accent ? 0 : parent.height - 1
            clip: true
            visible: control.enabled && !control.showPressed && !control.subtle
            Rectangle {
                width: parent.width
                height: Theme.radiusControl * 2
                y: control.accent ? 0 : 1 - height
                radius: Theme.radiusControl
                color: "transparent"
                border.width: 1
                border.color: control.accent ? "#1fffffff" : Theme.strokeControlBottom
            }
        }
        FocusRing {
            shown: control.visualFocus
        }
    }
}
