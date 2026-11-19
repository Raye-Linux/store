// SPDX-License-Identifier: GPL-3.0-or-later
// "Rate and review": stars, a title, the review and the name to sign
// it with. It says plainly where the review goes before it is sent.
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import QtQuick.Templates as T
import org.kde.kirigami as Kirigami

T.Popup {
    id: dialog

    property var app: null
    // the Reviews object of the page
    property var reviews: null
    property string error: ""
    // set after a click on Post, so empty fields don't nag before that
    property bool tried: false
    readonly property string problem: reviews ? reviews.problem(stars.value, title.text, body.text) : ""

    function ask(what) {
        app = what
        stars.value = 0
        title.text = ""
        body.text = ""
        // empty: the review is anonymous unless the person types a name
        name.text = ""
        error = ""
        tried = false
        open()
    }
    function post() {
        tried = true
        error = ""
        if (problem === "")
            reviews.submit(stars.value, title.text, body.text, name.text)
    }

    Connections {
        target: dialog.reviews
        function onSubmitted(ok, message) {
            if (!dialog.visible)
                return
            if (ok)
                dialog.close()
            else
                dialog.error = message
        }
    }

    parent: QQC2.Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(520, parent ? parent.width - 48 : 520)
    height: Math.min(implicitHeight, parent ? parent.height - 48 : implicitHeight)
    implicitHeight: implicitContentHeight + topPadding + bottomPadding
    modal: true
    dim: true
    focus: true
    padding: 0
    closePolicy: reviews && reviews.sending ? T.Popup.NoAutoClose : T.Popup.CloseOnEscape

    QQC2.Overlay.modal: Rectangle {
        color: Theme.smoke
        Behavior on opacity { NumberAnimation { duration: Theme.durationFast } }
    }
    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.durationFast }
        NumberAnimation { property: "scale"; from: 1.04; to: 1; duration: Theme.durationNormal; easing.type: Theme.easingEnter }
    }
    exit: Transition {
        NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.durationFaster }
    }

    background: Item {
        Glow { radius: Theme.radiusCard; spread: 24; offsetY: 8; color: Qt.alpha(Theme.shadow, Theme.night ? 0.55 : 0.18) }
        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusCard
            color: Theme.flyout
            border.width: 1
            border.color: Theme.strokeSurface
        }
        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 1
            height: footer.height
            radius: Theme.radiusCard
            color: Theme.flyoutFooter
            Rectangle { width: parent.width; height: Theme.radiusCard; color: parent.color }
        }
    }

    contentItem: ColumnLayout {
        spacing: 0

        Flickable {
            id: scroller
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredHeight: form.implicitHeight + 2 * Theme.s7
            contentHeight: form.implicitHeight + 2 * Theme.s7
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            QQC2.ScrollBar.vertical: ThinScrollBar {}

            ColumnLayout {
                id: form
                x: Theme.s7
                y: Theme.s7
                width: scroller.width - 2 * Theme.s7
                spacing: Theme.s4

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.s4
                    Kirigami.Icon {
                        source: dialog.app ? (dialog.app.iconSource || "application-x-executable") : ""
                        fallback: "application-x-executable"
                        Layout.preferredWidth: 40
                        Layout.preferredHeight: 40
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        Text {
                            Layout.fillWidth: true
                            text: i18nc("@title:window", "Rate and review")
                            font: Theme.subtitle
                            color: Theme.text
                            Accessible.role: Accessible.Heading
                        }
                        Text {
                            Layout.fillWidth: true
                            text: dialog.app ? dialog.app.name : ""
                            font: Theme.body
                            color: Theme.textSecondary
                            elide: Text.ElideRight
                        }
                    }
                }

                ColumnLayout {
                    Layout.topMargin: Theme.s2
                    spacing: Theme.s1
                    StarPicker {
                        id: stars
                        size: 28
                        Layout.leftMargin: -Theme.s1
                        KeyNavigation.tab: title
                    }
                    Text {
                        text: [i18nc("@info a rating of none yet", "Pick a rating"),
                               i18nc("@info a rating of 1 star out of 5", "Hated it"),
                               i18nc("@info a rating of 2 stars out of 5", "Didn't like it"),
                               i18nc("@info a rating of 3 stars out of 5", "It's OK"),
                               i18nc("@info a rating of 4 stars out of 5", "Liked it"),
                               i18nc("@info a rating of 5 stars out of 5", "Loved it")][stars.shown]
                        font: Theme.caption
                        color: stars.shown > 0 ? Theme.text : Theme.textTertiary
                    }
                }

                Label { text: i18nc("@label:textbox", "Title") }
                Field {
                    id: title
                    Layout.fillWidth: true
                    maximumLength: 70
                    placeholderText: i18nc("@info:placeholder", "Sum it up in a few words")
                    Accessible.name: i18nc("@label:textbox", "Title")
                    KeyNavigation.tab: body
                }

                Label { text: i18nc("@label:textbox", "Your review") }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 128
                    radius: Theme.radiusControl
                    color: body.activeFocus ? Theme.inputActive : bodyHover.hovered ? Theme.controlHover : Theme.control
                    border.width: 1
                    border.color: body.activeFocus ? Theme.strokeSurface : Theme.strokeControl
                    HoverHandler { id: bodyHover; cursorShape: Qt.IBeamCursor }
                    Flickable {
                        id: bodyScroll
                        anchors.fill: parent
                        anchors.margins: 1
                        contentHeight: body.implicitHeight
                        clip: true
                        boundsBehavior: Flickable.StopAtBounds
                        QQC2.ScrollBar.vertical: ThinScrollBar {}
                        TextEdit {
                            id: body
                            readonly property string placeholderText: i18nc("@info:placeholder", "What did you like? What could be better?")
                            width: bodyScroll.width
                            height: Math.max(implicitHeight, bodyScroll.height)
                            leftPadding: Theme.s4
                            rightPadding: Theme.s4
                            topPadding: Theme.s3
                            bottomPadding: Theme.s3
                            font: Theme.body
                            color: Theme.text
                            wrapMode: TextEdit.Wrap
                            selectByMouse: true
                            selectionColor: Theme.accent
                            selectedTextColor: Theme.textOnAccent
                            activeFocusOnTab: true
                            Accessible.role: Accessible.EditableText
                            Accessible.name: i18nc("@label:textbox", "Your review")
                            KeyNavigation.tab: name
                            KeyNavigation.backtab: title
                            // the server takes 3000 characters at most
                            onTextChanged: if (length > 3000) remove(3000, length)
                            onCursorRectangleChanged: {
                                if (cursorRectangle.y < bodyScroll.contentY)
                                    bodyScroll.contentY = cursorRectangle.y
                                else if (cursorRectangle.y + cursorRectangle.height > bodyScroll.contentY + bodyScroll.height)
                                    bodyScroll.contentY = cursorRectangle.y + cursorRectangle.height - bodyScroll.height
                            }
                            Text {
                                x: body.leftPadding
                                y: body.topPadding
                                width: body.width - body.leftPadding - body.rightPadding
                                visible: body.length === 0 && !body.preeditText
                                text: body.placeholderText
                                font: body.font
                                color: Theme.textTertiary
                                wrapMode: Text.Wrap
                            }
                        }
                    }
                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        anchors.leftMargin: Theme.radiusControl / 2
                        anchors.rightMargin: Theme.radiusControl / 2
                        height: body.activeFocus ? 2 : 1
                        color: body.activeFocus ? Theme.accent : Theme.strokeControlBottom
                    }
                }
                Text {
                    Layout.alignment: Qt.AlignRight
                    Layout.topMargin: -Theme.s2
                    visible: body.length > 2500
                    text: i18nc("@info characters used out of the most allowed", "%1/%2", body.length, 3000)
                    font: Theme.caption
                    color: Theme.textTertiary
                }

                Label { text: i18nc("@label:textbox", "Name shown with your review") }
                Field {
                    id: name
                    Layout.fillWidth: true
                    maximumLength: 40
                    placeholderText: i18nc("@info:placeholder", "Anonymous")
                    Accessible.name: i18nc("@label:textbox", "Name shown with your review")
                    KeyNavigation.tab: postButton
                }
                Text {
                    Layout.fillWidth: true
                    Layout.topMargin: -Theme.s2
                    text: i18n("Leave it empty to post without a name.")
                    font: Theme.caption
                    color: Theme.textTertiary
                    wrapMode: Text.Wrap
                }

                // where it goes, said before it goes
                RowLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: Theme.s2
                    spacing: Theme.s3
                    Glyph {
                        Layout.alignment: Qt.AlignTop
                        Layout.topMargin: 2
                        name: "eye"
                        size: 16
                        color: Theme.textSecondary
                    }
                    Text {
                        Layout.fillWidth: true
                        text: i18n("Your review is public. It's sent to the Open Desktop Ratings Service, which other app stores show too, with the app's version and your language. Apart from the name above, Raye sends nothing that says who you are.")
                        font: Theme.caption
                        color: Theme.textSecondary
                        wrapMode: Text.Wrap
                        lineHeightMode: Text.FixedHeight
                        lineHeight: Theme.captionLineHeight
                    }
                }

                Text {
                    Layout.fillWidth: true
                    visible: text !== ""
                    text: dialog.error !== "" ? dialog.error : dialog.tried ? dialog.problem : ""
                    font: Theme.caption
                    color: Theme.critical
                    wrapMode: Text.Wrap
                    Accessible.role: Accessible.AlertMessage
                }
            }
        }

        Item {
            id: footer
            Layout.fillWidth: true
            implicitHeight: 80
            RowLayout {
                anchors.fill: parent
                anchors.margins: Theme.s7
                spacing: Theme.s3
                Button {
                    id: postButton
                    Layout.fillWidth: true
                    accent: true
                    enabled: dialog.reviews !== null && !dialog.reviews.sending
                    text: dialog.reviews && dialog.reviews.sending ? i18nc("@action:button while a review is sent", "Posting…")
                                                                     : i18nc("@action:button send a review", "Post")
                    onClicked: dialog.post()
                }
                Button {
                    Layout.fillWidth: true
                    enabled: !dialog.reviews || !dialog.reviews.sending
                    text: i18n("Cancel")
                    onClicked: dialog.close()
                }
            }
        }
    }
    onOpened: title.forceActiveFocus()

    component Label: Text {
        Layout.fillWidth: true
        Layout.topMargin: Theme.s2
        Layout.bottomMargin: -Theme.s2
        font: Theme.captionStrong
        color: Theme.text
    }

    component Field: T.TextField {
        id: field
        implicitHeight: Theme.controlHeight
        leftPadding: Theme.s4
        rightPadding: Theme.s4
        verticalAlignment: TextInput.AlignVCenter
        font: Theme.body
        color: Theme.text
        selectionColor: Theme.accent
        selectedTextColor: Theme.textOnAccent
        placeholderTextColor: Theme.textTertiary
        selectByMouse: true
        hoverEnabled: true
        Accessible.role: Accessible.EditableText
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
    }
}
