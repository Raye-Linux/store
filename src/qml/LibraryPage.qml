// SPDX-License-Identifier: GPL-3.0-or-later
// The Library: what downloads now, the updates waiting, every installed
// app with Open and Uninstall, and what happened lately.
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.raye.store

PageScroll {
    id: lib
    // a list reads best not too wide
    maxWidth: 1080
    signal open(var app)
    signal browseApps()

    // the Raye account; "Your devices" stays hidden until there is one
    property bool accountAvailable: false

    // an installed row as the installer and the pages want it
    function asApp(model) {
        const found = catalog.find(model.appId)
        return found.appId !== undefined ? found
             : { appId: model.appId, name: model.name, iconSource: model.iconSource || model.icon,
                 source: model.source, ref: model.ref, desktopId: model.desktopId }
    }
    function ago(time) {
        const secs = Math.max(0, (Date.now() - new Date(time).getTime()) / 1000)
        if (secs < 60) return i18nc("@info time", "Just now")
        if (secs < 3600) return i18ncp("@info time", "One minute ago", "%1 minutes ago", Math.floor(secs / 60))
        if (secs < 86400) return i18ncp("@info time", "One hour ago", "%1 hours ago", Math.floor(secs / 3600))
        return i18ncp("@info time", "Yesterday", "%1 days ago", Math.floor(secs / 86400))
    }
    Component.onCompleted: installer.history.reload()

    // title and the update buttons
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.s3
        Text {
            Layout.fillWidth: true
            text: i18n("Library")
            font: Theme.title
            color: Theme.text
        }
        Button {
            glyph: "refresh"
            text: installer.checking ? i18nc("@info:status", "Checking…") : i18nc("@action:button", "Get updates")
            enabled: !installer.checking
            // checks, then installs what it found, with its bars below
            onClicked: installer.getUpdates()
        }
        Button {
            accent: installer.flatpakUpdates > 0
            text: installer.busy && installer.working === "*" ? i18n("Updating…") : i18n("Update all")
            enabled: !installer.busy && installer.flatpakUpdates > 0
            onClicked: installer.updateAll()
        }
    }

    ListRow {
        Layout.fillWidth: true
        Layout.topMargin: -Theme.s5
        visible: lib.accountAvailable
        iconSource: Qt.resolvedUrl("icons/devices.svg")
        title: i18nc("@title the PCs of the Raye account", "Your devices")
        subtitle: i18n("The apps on your other PCs")
        Glyph { name: "chevron-right"; size: 14; color: Theme.textSecondary }
    }

    InfoBar {
        Layout.fillWidth: true
        Layout.topMargin: -Theme.s5
        visible: installer.updatesError !== ""
        severity: "warning"
        text: installer.updatesError
        closable: false
    }

    // Downloads: what installs, updates or waits, with its bar and Cancel
    ColumnLayout {
        Layout.fillWidth: true
        visible: installer.jobs.count > 0
        spacing: Theme.s2
        SectionHeader { Layout.fillWidth: true; Layout.bottomMargin: Theme.s2; title: i18n("Downloads") }
        Repeater {
            model: installer.jobs
            delegate: ListRow {
                required property var model
                Layout.fillWidth: true
                implicitHeight: 72
                iconSource: model.icon
                title: model.name
                wideTrailing: true
                focusPolicy: Qt.NoFocus
                JobStatus {
                    Layout.fillWidth: true
                    compact: true
                    job: ({state: model.state, action: model.action, percent: model.percent,
                           progressText: model.progressText, cancellable: model.cancellable, cancelling: model.cancelling,
                           position: model.position})
                    onCancel: installer.cancelJob(model.jobId)
                }
            }
        }
    }

    // Updates available
    ColumnLayout {
        Layout.fillWidth: true
        visible: installer.updates.count > 0
        spacing: Theme.s2
        SectionHeader {
            Layout.fillWidth: true
            Layout.bottomMargin: Theme.s2
            title: i18np("One update available", "%1 updates available", installer.updates.count)
        }
        Repeater {
            model: installer.updates
            delegate: ListRow {
                required property var model
                Layout.fillWidth: true
                iconSource: model.icon
                title: model.name || model.appId
                subtitle: model.oldVersion && model.newVersion ? i18nc("@info version 1.2 → 1.3", "Version %1 → %2", model.oldVersion, model.newVersion)
                        : model.newVersion ? i18nc("@info", "Version %1", model.newVersion) : ""
                onClicked: lib.open(lib.asApp(model))
                Button {
                    text: model.queued ? i18n("Waiting…") : i18nc("@action:button", "Update")
                    enabled: !model.queued
                    onClicked: installer.update({ appId: model.appId, name: model.name, iconSource: model.icon,
                                                  source: model.source, ref: model.ref })
                }
            }
        }
    }

    // installed apps
    ColumnLayout {
        Layout.fillWidth: true
        spacing: Theme.s2
        RowLayout {
            Layout.fillWidth: true
            Layout.bottomMargin: Theme.s3
            spacing: Theme.s3
            SectionHeader { Layout.fillWidth: true; title: i18nc("@title", "Installed") }
            Repeater {
                model: [["", i18nc("@option:radio show", "All")], ["app", i18nc("@option:radio show", "Apps")],
                        ["game", i18nc("@option:radio show", "Games")]]
                delegate: Chip {
                    required property var modelData
                    text: modelData[1]
                    selected: library.kind === modelData[0]
                    onClicked: library.kind = modelData[0]
                }
            }
        }
        Repeater {
            model: library
            delegate: ListRow {
                id: installedRow
                required property var model
                Layout.fillWidth: true
                iconSource: model.iconSource
                title: model.name
                subtitle: [model.kind === "game" ? i18nc("@info kind of app", "Game") : i18nc("@info kind of app", "App"),
                           model.systemCopy !== "" ? i18nc("@info where the app comes from", "System") : Format.source(model)].join("  ·  ")
                onClicked: lib.open(lib.asApp(model))
                Button {
                    text: i18n("Open")
                    onClicked: installer.open(lib.asApp(installedRow.model))
                }
                Button {
                    id: moreButton
                    subtle: true
                    glyph: "more"
                    implicitWidth: 32
                    Accessible.name: i18nc("@action:button more actions for an app", "More")
                    onClicked: menu.popup(moreButton, moreButton.width - menu.width, moreButton.height + 4)
                    Menu {
                        id: menu
                        QQC2.Action {
                            text: i18nc("@action:inmenu", "Show in Store")
                            onTriggered: lib.open(lib.asApp(installedRow.model))
                        }
                        QQC2.Action {
                            enabled: installedRow.model.systemCopy === ""
                            text: i18nc("@action:inmenu", "Uninstall…")
                            onTriggered: confirmUninstall.ask(lib.asApp(installedRow.model))
                        }
                    }
                }
            }
        }
        EmptyState {
            Layout.fillWidth: true
            Layout.topMargin: Theme.s8
            Layout.bottomMargin: Theme.s8
            visible: library.count === 0 && !catalog.loading
            glyph: "library"
            title: library.kind === "game" ? i18nc("@title", "No games yet") : i18nc("@title", "No apps yet")
            message: i18n("Apps you install from the Store show up here.")
            Button {
                accent: true
                text: i18nc("@action:button", "Browse apps")
                onClicked: lib.browseApps()
            }
        }
    }

    // recent activity
    ColumnLayout {
        Layout.fillWidth: true
        visible: installer.history.count > 0
        spacing: Theme.s2
        SectionHeader { Layout.fillWidth: true; Layout.bottomMargin: Theme.s2; title: i18nc("@title", "Recent activity") }
        Repeater {
            model: installer.history
            delegate: ListRow {
                required property var model
                required property int index
                visible: index < 6
                Layout.fillWidth: true
                implicitHeight: 56
                iconSource: model.icon
                title: model.name
                focusPolicy: Qt.NoFocus
                subtitle: (model.result === "done"
                           ? (model.action === "remove" ? i18nc("@info", "Uninstalled") : model.action === "update" ? i18nc("@info", "Updated") : i18nc("@info", "Installed"))
                           : model.result === "cancelled" ? i18nc("@info", "Canceled")
                           : (model.action === "remove" ? i18nc("@info", "Couldn't uninstall") : model.action === "update" ? i18nc("@info", "Couldn't update") : i18nc("@info", "Couldn't install")))
                          + "  ·  " + lib.ago(model.time)
                Glyph {
                    name: model.result === "done" ? "check" : "close"
                    size: 16
                    color: model.result === "done" ? Theme.success : model.result === "cancelled" ? Theme.textSecondary : Theme.critical
                }
            }
        }
    }

    UninstallDialog { id: confirmUninstall }
}
