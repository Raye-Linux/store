// SPDX-License-Identifier: GPL-3.0-or-later
// The small window a .deb file opens in: the app, Install, how far it
// is, and Open once it is done. `deb` is a DebFile (src/debfile.h).
import QtQuick
import QtQuick.Layouts
import QtQuick.Window
import org.kde.kirigami as Kirigami
import org.raye.store

Window {
    id: window

    readonly property int ready: 1
    readonly property int installing: 2
    readonly property int done: 3
    readonly property int failed: 4
    readonly property bool canInstall: deb.state === ready && deb.relation <= 1
    property bool showDetails: false

    title: i18n("App Installer")
    color: Theme.backdropSolid
    // a fixed size that follows what it shows, like a setup window
    width: 600
    height: page.implicitHeight
    minimumWidth: 600
    maximumWidth: 600
    minimumHeight: page.implicitHeight
    maximumHeight: page.implicitHeight
    visible: true

    // apt must not stop half way
    onClosing: close => close.accepted = deb.state !== installing

    function headline() {
        switch (deb.state) {
        case 0: return deb.fileName
        case installing: return i18n("Installing %1", deb.name)
        case done: return i18n("%1 is ready", deb.name)
        case failed: return deb.errorTitle
        }
        switch (deb.relation) {
        case 1: return i18n("Update %1?", deb.name)
        case 2: return i18n("%1 is already installed", deb.name)
        case 3: return i18n("A newer version of %1 is installed", deb.name)
        }
        return i18n("Install %1?", deb.name)
    }

    function facts() {
        let parts = []
        // replacing something already installed: say what, by its real name
        if (deb.relation !== 0 && deb.packageName !== "" && deb.state !== done)
            parts.push(deb.packageName)
        if (deb.version !== "")
            parts.push(i18n("Version %1", deb.version))
        if (deb.installedVersion !== "" && (deb.relation === 1 || deb.relation === 3) && deb.state !== done)
            parts.push(i18n("On this PC: %1", deb.installedVersion))
        if (deb.sizeText !== "")
            parts.push(deb.sizeText)
        return parts.join("  ·  ")
    }

    Shortcut {
        sequence: "Escape"
        enabled: deb.state !== installing
        onActivated: window.close()
    }

    ColumnLayout {
        id: page
        width: parent.width
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: Theme.s8
            Layout.leftMargin: Theme.s8
            Layout.rightMargin: Theme.s8
            spacing: Theme.s6

            // the app's icon on a card, like the Store's own tiles
            Rectangle {
                Layout.alignment: Qt.AlignTop
                implicitWidth: 88
                implicitHeight: 88
                radius: Theme.radiusHero
                color: Theme.cardSolid
                border.width: 1
                border.color: Theme.cardBorder
                Kirigami.Icon {
                    anchors.centerIn: parent
                    width: Theme.cardIcon
                    height: Theme.cardIcon
                    source: deb.icon.toString() !== "" ? deb.icon : "application-x-deb"
                    fallback: "package-x-generic"
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                spacing: Theme.s2

                Text {
                    Layout.fillWidth: true
                    text: window.headline()
                    textFormat: Text.PlainText
                    font: Theme.title
                    color: Theme.text
                    wrapMode: Text.Wrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                    Accessible.role: Accessible.Heading
                }
                Text {
                    Layout.fillWidth: true
                    visible: text !== ""
                    // a file that could not be read: at least its name
                    text: deb.state === 0 ? ""
                          : deb.packageName === "" ? deb.fileName
                          : deb.publisher !== "" ? i18n("Publisher: %1", deb.publisher) : ""
                    textFormat: Text.PlainText
                    font: Theme.body
                    color: Theme.textSecondary
                    elide: Text.ElideRight
                }
                Text {
                    Layout.fillWidth: true
                    visible: text !== ""
                    text: deb.state === 0 ? "" : window.facts()
                    textFormat: Text.PlainText
                    font: Theme.body
                    color: Theme.textSecondary
                    elide: Text.ElideRight
                }
                Text {
                    Layout.fillWidth: true
                    Layout.topMargin: Theme.s3
                    visible: text !== ""
                    text: deb.state === 0 ? "" : deb.summary
                    textFormat: Text.PlainText
                    font: Theme.body
                    color: Theme.text
                    wrapMode: Text.Wrap
                    maximumLineCount: 3
                    elide: Text.ElideRight
                }
            }
        }

        // what is going on, under the app
        ColumnLayout {
            Layout.fillWidth: true
            Layout.margins: Theme.s8
            Layout.topMargin: Theme.s7
            spacing: Theme.s3

            // the Store has its own copy, set up for Raye: advised, never forced
            InfoBar {
                Layout.fillWidth: true
                visible: deb.storeAppId !== "" && (window.canInstall || deb.state === failed)
                severity: "info"
                closable: false
                text: i18n("%1 is also in the Store, and that's the one we recommend: it updates by itself and is set up for Raye.", deb.name)
                Button {
                    subtle: true
                    text: i18n("Get it from the Store")
                    onClicked: {
                        if (deb.openInStore())
                            Qt.quit()
                    }
                }
            }

            // what installing it takes away, in plain words, before Install
            InfoBar {
                Layout.fillWidth: true
                visible: window.canInstall && deb.replaces.length > 0
                severity: "info"
                closable: false
                text: deb.replacesText
            }

            InfoBar {
                Layout.fillWidth: true
                visible: window.canInstall
                severity: "warning"
                closable: false
                text: i18n("This app doesn't come from the Store, so Raye can't check it. Install it only if you trust where you got it.")
            }

            RowLayout {
                Layout.fillWidth: true
                visible: deb.state === 0 || deb.state === installing
                Text {
                    Layout.fillWidth: true
                    text: deb.state === 0 ? i18n("Reading the app…") : deb.phaseText
                    font: Theme.body
                    color: Theme.textSecondary
                }
                Text {
                    visible: deb.state === installing && deb.percent >= 0
                    text: i18nc("how far an install is", "%1%", deb.percent)
                    font: Theme.body
                    color: Theme.textSecondary
                }
            }

            // the Store's thin accent bar; a segment runs while it cannot say
            Item {
                id: bar
                readonly property bool indeterminate: deb.state === 0 || deb.percent < 0
                visible: deb.state === 0 || deb.state === installing
                Layout.fillWidth: true
                implicitHeight: Theme.progressHeight
                clip: true
                Accessible.role: Accessible.ProgressBar
                Accessible.name: deb.phaseText

                Rectangle {
                    anchors.fill: parent
                    radius: height / 2
                    color: Theme.strokeSurface
                }
                Rectangle {
                    visible: !bar.indeterminate
                    height: parent.height
                    radius: height / 2
                    width: Math.max(height, parent.width * Math.min(100, Math.max(0, deb.percent)) / 100)
                    color: Theme.accent
                    Behavior on width { NumberAnimation { duration: Theme.durationNormal; easing.type: Theme.easingFast } }
                }
                Rectangle {
                    id: runner
                    visible: bar.indeterminate
                    height: parent.height
                    radius: height / 2
                    width: parent.width * 0.3
                    color: Theme.accent
                    SequentialAnimation on x {
                        running: bar.visible && bar.indeterminate && !Theme.animationsOff
                        loops: Animation.Infinite
                        NumberAnimation { from: -runner.width; to: bar.width; duration: 1400; easing.type: Easing.InOutQuad }
                    }
                }
            }

            RowLayout {
                visible: deb.state === done
                spacing: Theme.s3
                Rectangle {
                    implicitWidth: 20
                    implicitHeight: 20
                    radius: 10
                    color: Theme.success
                    Glyph {
                        anchors.centerIn: parent
                        name: "check"
                        size: 14
                        color: Theme.night ? Theme.backdropSolid : "#ffffff"
                    }
                }
                Text {
                    text: i18n("Find it in Start.")
                    font: Theme.body
                    color: Theme.text
                }
            }

            InfoBar {
                Layout.fillWidth: true
                visible: deb.state === failed
                severity: "error"
                closable: false
                text: deb.errorText
                Button {
                    visible: deb.details !== ""
                    subtle: true
                    text: window.showDetails ? i18n("Hide details") : i18n("Show details")
                    onClicked: window.showDetails = !window.showDetails
                }
            }

            // apt's words and the package's own: plain text, never markup
            Text {
                Layout.fillWidth: true
                visible: deb.state === failed && window.showDetails && deb.details !== ""
                text: deb.details
                textFormat: Text.PlainText
                font.family: "Cascadia Mono"
                font.pointSize: 8
                color: Theme.textSecondary
                wrapMode: Text.WrapAnywhere
                maximumLineCount: 10
                elide: Text.ElideLeft
            }
        }

        // the buttons, on the footer band of a Raye dialog
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: footer.implicitHeight + 2 * Theme.s7
            color: Theme.flyoutFooter
            Rectangle {
                width: parent.width
                height: 1
                color: Theme.divider
            }
            RowLayout {
                id: footer
                anchors.right: parent.right
                anchors.rightMargin: Theme.s7
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.s3

                Button {
                    id: closeButton
                    implicitWidth: Theme.controlMinWidth
                    accent: deb.state === failed
                            || (deb.state !== 0 && deb.state !== installing && !window.canInstall && !deb.canOpen)
                    enabled: deb.state !== installing
                    text: window.canInstall || deb.state === 0 ? i18n("Cancel") : i18n("Close")
                    onClicked: window.close()
                }
                Button {
                    id: installButton
                    visible: window.canInstall
                    implicitWidth: Theme.controlMinWidth
                    accent: true
                    text: deb.relation === 1 ? i18n("Update") : i18n("Install")
                    onClicked: deb.install()
                }
                Button {
                    id: openButton
                    visible: deb.canOpen && (deb.state === done || deb.state === ready)
                    implicitWidth: Theme.controlMinWidth
                    accent: true
                    text: i18n("Open")
                    onClicked: {
                        if (deb.open())
                            Qt.quit()
                    }
                }
            }
        }
    }

    // the button that does it gets the keyboard
    Connections {
        target: deb
        function onChanged() {
            if (installButton.visible)
                installButton.forceActiveFocus()
            else if (openButton.visible)
                openButton.forceActiveFocus()
            else if (closeButton.enabled)
                closeButton.forceActiveFocus()
        }
    }
}
