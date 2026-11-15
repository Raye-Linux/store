// SPDX-License-Identifier: GPL-3.0-or-later
// The button that matters for one app: Install, then what the install
// is doing, then Open. Follows installs made anywhere.
import QtQuick

Button {
    id: get

    property var app: null
    // where this app's install, update or removal is
    readonly property var job: installer.revision >= 0 && app && app.ref ? installer.job(app.ref) : ({ state: 0 })
    readonly property bool working: job.state >= 1 && job.state <= 4
    property bool installed: false
    function refresh() { installed = app && app.ref ? catalog.isInstalled(app.ref) : false }
    onAppChanged: refresh()
    Connections {
        target: catalog
        function onInstalledChanged() { get.refresh() }
    }
    Connections {
        target: installer
        function onFinished(ok, ref, message) { if (get.app && ref === get.app.ref) get.refresh() }
    }

    accent: true
    enabled: app !== null && !working
    implicitWidth: Math.max(Theme.controlMinWidth, implicitContentWidth + leftPadding + rightPadding)
    text: working ? (job.state === 4 ? i18nc("@info:status", "Removing…")
                     : job.state === 1 ? i18nc("@info:status", "Waiting…")
                     : job.state === 2 && job.percent >= 0 ? i18nc("@info:status percent done", "%1%", job.percent)
                     : i18nc("@info:status", "Installing…"))
        : installed ? i18nc("@action:button start the app", "Open")
        : i18nc("@action:button", "Install")
    Accessible.name: app ? (installed ? i18nc("@action:button", "Open %1", app.name)
                                      : i18nc("@action:button", "Install %1", app.name)) : text
    onClicked: installed ? installer.open(app) : installer.get(app)
}
