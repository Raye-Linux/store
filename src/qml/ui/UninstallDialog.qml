// SPDX-License-Identifier: GPL-3.0-or-later
// "Uninstall X?", for the app's page and the Library. An app that is
// open is closed first, and the question says so.
import QtQuick

ConfirmDialog {
    id: dialog
    property var app: null
    property bool running: false

    function ask(what) {
        app = what
        running = installer.isRunning(what)
        open()
    }

    title: app ? i18n("Uninstall %1?", app.name) : ""
    message: running && app
             ? i18n("%1 is open. It will be closed, then removed from this PC with what only it needed. Your own files are kept.", app.name)
             : i18n("It will be removed from this PC, with what only it needed. Your own files are kept.")
    actionText: running ? i18n("Close and uninstall") : i18n("Uninstall")
    onAccepted: {
        if (running)
            installer.closeAndUninstall(app)
        else
            installer.uninstall(app)
    }
}
