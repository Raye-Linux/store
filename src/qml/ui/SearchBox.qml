// SPDX-License-Identifier: GPL-3.0-or-later
// The search box in the Store's header. Esc empties it; the magnifier
// at the right searches now.
import QtQuick
import QtQuick.Templates as T

T.TextField {
    id: field

    signal searchNow()
    signal dismissSuggestions()
    signal cleared()
    signal leave()
    property bool suggestionsShown: false

    implicitWidth: Theme.searchMaxWidth
    implicitHeight: 36
    leftPadding: Theme.s4
    rightPadding: 40 + (text !== "" ? 28 : 0)
    verticalAlignment: TextInput.AlignVCenter
    font: Theme.body
    color: Theme.text
    selectionColor: Theme.accent
    selectedTextColor: Theme.textOnAccent
    placeholderText: i18nc("@info:placeholder", "Search apps and games")
    placeholderTextColor: Theme.textTertiary
    selectByMouse: true
    hoverEnabled: true
    Accessible.role: Accessible.EditableText
    Accessible.name: placeholderText

    Keys.onEscapePressed: event => {
        if (suggestionsShown) {
            dismissSuggestions()
            event.accepted = true
        } else if (text !== "") {
            text = ""
            cleared()
            event.accepted = true
        } else {
            leave()
            event.accepted = true
        }
    }

    Text {
        x: field.leftPadding
        width: field.width - field.leftPadding - field.rightPadding
        anchors.verticalCenter: parent.verticalCenter
        visible: field.text === "" && !field.preeditText
        text: field.placeholderText
        font: field.font
        color: field.placeholderTextColor
        elide: Text.ElideRight
    }

    background: Rectangle {
        radius: Theme.radiusControl
        color: field.activeFocus ? Theme.inputActive : field.hovered ? Theme.controlHover : Theme.control
        border.width: 1
        border.color: field.activeFocus ? Theme.strokeSurface : Theme.strokeControl
        Behavior on color { ColorAnimation { duration: Theme.durationFaster } }
        // the line under the box: thin at rest, accent when typing
        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.leftMargin: Theme.radiusControl / 2
            anchors.rightMargin: Theme.radiusControl / 2
            height: field.activeFocus ? 2 : 1
            color: field.activeFocus ? Theme.accent : Theme.strokeControlBottom
        }
    }

    Button {
        id: clear
        visible: field.text !== ""
        subtle: true
        glyph: "close"
        glyphSize: 12
        width: 28
        height: 28
        anchors.right: magnifier.left
        anchors.verticalCenter: parent.verticalCenter
        focusPolicy: Qt.NoFocus
        Accessible.name: i18nc("@action:button empty the search box", "Clear")
        onClicked: {
            field.text = ""
            field.textEdited()
        }
    }
    Button {
        id: magnifier
        subtle: true
        glyph: "search"
        glyphSize: 16
        width: 32
        height: 28
        anchors.right: parent.right
        anchors.rightMargin: 2
        anchors.verticalCenter: parent.verticalCenter
        focusPolicy: Qt.NoFocus
        Accessible.name: i18nc("@action:button", "Search")
        onClicked: field.searchNow()
    }
}
