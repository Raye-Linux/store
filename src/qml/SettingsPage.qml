// SPDX-License-Identifier: GPL-3.0-or-later
// The Store's settings: how apps update, what search shows, and which
// Store this is.
import QtQuick
import QtQuick.Layouts
import org.raye.store

PageScroll {
    id: page
    maxWidth: 820
    spacing: Theme.s7

    Text {
        Layout.fillWidth: true
        text: i18nc("@title the Store's settings page", "Settings")
        font: Theme.title
        color: Theme.text
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: Theme.s4
        SectionHeader { Layout.fillWidth: true; title: i18nc("@title", "App updates") }
        Choice {
            title: i18n("Update apps automatically")
            description: i18n("Once a day, when this PC is plugged in and idle. Raye Update looks after the system.")
            checked: storeSettings.autoUpdate
            onToggled: on => storeSettings.autoUpdate = on
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: Theme.s4
        SectionHeader { Layout.fillWidth: true; title: i18nc("@title", "Search") }
        Choice {
            title: i18n("Show system packages")
            description: i18n("Also list tools and other parts of the system when you search.")
            checked: storeSettings.showSystemPackages
            onToggled: on => storeSettings.showSystemPackages = on
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: Theme.s4
        SectionHeader { Layout.fillWidth: true; title: i18nc("@title", "About this app") }
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: about.implicitHeight + 2 * Theme.s5
            radius: Theme.radiusControl
            color: Theme.cardSolid
            border.width: 1
            border.color: Theme.strokeSurface
            ColumnLayout {
                id: about
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.margins: Theme.s5
                spacing: Theme.s1
                Text {
                    text: i18n("Store")
                    font: Theme.bodyStrong
                    color: Theme.text
                }
                Text {
                    text: i18nc("@info %1 is a version number", "Version %1", Qt.application.version)
                    font: Theme.caption
                    color: Theme.textSecondary
                }
            }
        }
    }

    // one line of a section: what it does and a switch
    component Choice: Rectangle {
        id: choice
        property string title
        property string description
        property bool checked: false
        signal toggled(bool on)

        Layout.fillWidth: true
        implicitHeight: Math.max(64, words.implicitHeight + 2 * Theme.s4)
        radius: Theme.radiusControl
        color: choiceHover.hovered ? Theme.cardSolidHover : Theme.cardSolid
        border.width: 1
        border.color: Theme.strokeSurface
        activeFocusOnTab: true
        Accessible.role: Accessible.CheckBox
        Accessible.name: title
        Accessible.description: description
        Accessible.checked: checked
        Keys.onSpacePressed: choice.toggled(!choice.checked)
        HoverHandler { id: choiceHover; cursorShape: Qt.PointingHandCursor }
        TapHandler { onTapped: choice.toggled(!choice.checked) }
        FocusRing { shown: choice.activeFocus }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Theme.s5
            anchors.rightMargin: Theme.s5
            spacing: Theme.s5
            ColumnLayout {
                id: words
                Layout.fillWidth: true
                spacing: Theme.s1
                Text {
                    Layout.fillWidth: true
                    text: choice.title
                    font: Theme.body
                    color: Theme.text
                    wrapMode: Text.Wrap
                }
                Text {
                    Layout.fillWidth: true
                    text: choice.description
                    font: Theme.caption
                    color: Theme.textSecondary
                    wrapMode: Text.Wrap
                }
            }
            Text {
                text: choice.checked ? i18nc("@info a switch is on", "On") : i18nc("@info a switch is off", "Off")
                font: Theme.body
                color: Theme.textSecondary
            }
            Rectangle {
                implicitWidth: 40
                implicitHeight: 20
                radius: 10
                color: choice.checked ? Theme.accent : "transparent"
                border.width: choice.checked ? 0 : 1
                border.color: Theme.textSecondary
                Rectangle {
                    width: choice.checked ? 12 : 10
                    height: width
                    radius: width / 2
                    x: choice.checked ? parent.width - width - 4 : 5
                    anchors.verticalCenter: parent.verticalCenter
                    color: choice.checked ? Theme.textOnAccent : Theme.textSecondary
                    Behavior on x { NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easingEnter } }
                }
            }
        }
    }
}
