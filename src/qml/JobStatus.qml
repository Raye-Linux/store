// SPDX-License-Identifier: GPL-3.0-or-later
// Where an install, update or removal is: what it does, how far, how
// much is downloaded, and Cancel. Used on the app's page and in the
// Library's downloads.
import QtQuick
import QtQuick.Layouts
import org.raye.store

ColumnLayout {
    id: status
    // installer.job(ref), or a row of installer.jobs
    property var job: ({})
    property bool compact: false
    signal cancel()

    // Installer.State: Idle, Pending, Downloading, Installing, Removing
    readonly property int jobState: job && job.state !== undefined ? job.state : 0
    readonly property int percent: job && job.percent !== undefined ? job.percent : -1
    readonly property bool active: jobState >= 1 && jobState <= 4

    spacing: Theme.s2
    visible: active

    readonly property bool cancelling: job && job.cancelling === true

    function stateText() {
        if (status.cancelling)
            return i18n("Canceling…")
        const update = status.job && status.job.action === "update"
        switch (status.jobState) {
        case 1:
            return status.job.position > 0 ? i18n("Waiting…") : i18n("Starting…")
        case 2:
            if (status.percent >= 0)
                return update ? i18n("Updating · %1%", status.percent) : i18n("Downloading · %1%", status.percent)
            return update ? i18n("Updating…") : i18n("Downloading…")
        case 3:
            if (update)
                return i18n("Finishing the update…")
            return status.percent >= 0 ? i18n("Installing · %1%", status.percent) : i18n("Installing…")
        case 4:
            return i18n("Removing…")
        }
        return ""
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.s3
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 0
            Text {
                Layout.fillWidth: true
                text: status.stateText()
                font: status.compact ? Theme.body : Theme.bodyStrong
                color: Theme.text
                elide: Text.ElideRight
            }
            Text {
                Layout.fillWidth: true
                text: status.job && status.job.progressText ? status.job.progressText : ""
                visible: text.length > 0 && status.jobState === 2 && !status.cancelling
                font: Theme.caption
                color: Theme.textSecondary
                elide: Text.ElideRight
            }
            // a Windows program whose setup cannot run out of sight finishes
            // in its own window: say so on a line of its own that wraps, so
            // nothing is cut off
            Text {
                Layout.fillWidth: true
                text: i18n("Finish in the setup window that opened.")
                visible: status.jobState === 3 && status.job && status.job.source === "windows"
                         && status.job.setupWindow === true && !status.cancelling
                font: Theme.caption
                color: Theme.textSecondary
                wrapMode: Text.WordWrap
            }
        }
        Button {
            text: i18n("Cancel")
            glyph: "close"
            glyphSize: 12
            // a removal, or the last step of an install, cannot be stopped
            enabled: status.job && status.job.cancellable === true && status.jobState !== 4
            visible: status.jobState !== 4
            Accessible.description: i18n("Stop and leave nothing installed")
            onClicked: status.cancel()
        }
    }

    // a thin bar in the accent colour, like the Microsoft Store's; a
    // segment that runs back and forth while it cannot say how far
    Item {
        id: bar
        // an install that says how far it is (a Windows setup) shows it
        readonly property bool indeterminate: status.jobState === 1 || status.jobState === 4
                                              || status.percent < 0 || status.cancelling
        Layout.fillWidth: true
        implicitHeight: Theme.progressHeight
        clip: true
        Accessible.role: Accessible.ProgressBar
        Accessible.name: status.stateText()

        Rectangle {
            anchors.fill: parent
            radius: height / 2
            color: Theme.strokeSurface
        }
        Rectangle {
            visible: !bar.indeterminate
            height: parent.height
            radius: height / 2
            width: Math.max(height, parent.width * Math.min(100, Math.max(0, status.percent)) / 100)
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0; color: Theme.accent }
                GradientStop { position: 1; color: Theme.accentGlowEnd }
            }
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
                running: bar.indeterminate && status.active && !Theme.animationsOff
                loops: Animation.Infinite
                NumberAnimation { from: -runner.width; to: bar.width; duration: 1400; easing.type: Easing.InOutQuad }
            }
        }
    }
}
