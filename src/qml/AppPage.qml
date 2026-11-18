// SPDX-License-Identifier: GPL-3.0-or-later
// One app: what it is, who makes it, the button that matters, then its
// screenshots, description, ratings, what's new and the details.
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import QtQuick.Effects
import org.kde.kirigami as Kirigami
import org.raye.store
import org.raye.store.data

Flickable {
    id: page
    WheelScroll { target: page }
    property var app: null
    signal back()
    signal open(var app)

    // where this app's install, update or removal is; follows every change
    readonly property var job: installer.revision >= 0 && app ? installer.job(app.ref) : ({state: 0})
    readonly property bool working: job.state >= 1 && job.state <= 4
    property bool installed: false
    // a game that can't run here: nothing to install
    readonly property bool blocked: app !== null && app !== undefined && app.windowsState === "blocked"
    // the system's own copy: it opens from here, Raye Update looks after it
    property bool systemCopy: false
    function refresh() {
        installed = app ? catalog.isInstalled(app.ref) : false
        systemCopy = app ? catalog.systemCopy(app.ref) !== "" : false
    }
    // opened from a link or the search: Install is ready for Enter
    onAppChanged: {
        refresh()
        contentY = 0
        descriptionExpanded = false
        reachedEnd = false
        if (app && !installed)
            Qt.callLater(() => installButton.forceActiveFocus())
    }
    // installed or removed anywhere, the page follows
    Connections {
        target: catalog
        function onInstalledChanged() { page.refresh() }
        function onModelReset() { page.refresh() }
    }
    Connections {
        target: installer
        function onFinished(ok, ref, message) { if (page.app && ref === page.app.ref) page.refresh() }
    }

    property bool descriptionExpanded: false
    // "People also like" is made once the page scrolls near its end
    property bool reachedEnd: false
    onContentYChanged: if (!reachedEnd && contentY + height > contentHeight - 900) reachedEnd = true
    onContentHeightChanged: if (!reachedEnd && contentHeight > 0 && contentHeight < height + 900) reachedEnd = true
    CatalogFilter {
        id: alsoLike
        kind: page.app ? page.app.kind : ""
        category: page.app ? Format.mainCategory(page.app.categories) : ""
        limit: 17
        sourceModel: page.reachedEnd && page.app ? catalog : null
    }
    readonly property var shots: app && app.screenshots ? app.screenshots : []
    readonly property var release: app && app.releases && app.releases.length > 0 ? app.releases[0] : null
    readonly property bool wide: width >= 1000
    readonly property real pad: Theme.contentPadding
    // text lines stay short enough to read on a wide screen
    readonly property real usable: Math.min(width - 2 * pad, 1280)

    contentWidth: width
    contentHeight: body.y + body.implicitHeight + pad
    clip: true
    boundsBehavior: Flickable.StopAtBounds
    QQC2.ScrollBar.vertical: ThinScrollBar {}

    // the band across the top: the app's first screenshot, blurred and
    // dimmed, fading into the page; the app's colour without one
    Item {
        id: band
        width: page.width
        height: 300
        clip: true
        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                GradientStop { position: 0; color: Theme.mix(Theme.layerSolid, Format.brand(page.app), Theme.night ? 0.35 : 0.2) }
                GradientStop { position: 1; color: Theme.layerSolid }
            }
        }
        Image {
            id: bandShot
            anchors.fill: parent
            visible: false
            source: page.shots.length > 0 ? (page.shots[0].thumb || page.shots[0].full) : ""
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            cache: false
            // blurred: a small picture is enough
            sourceSize.width: 360
        }
        MultiEffect {
            anchors.fill: parent
            source: bandShot
            visible: bandShot.status === Image.Ready
            autoPaddingEnabled: false
            blurEnabled: true
            blurMax: 64
            blur: 1
            saturation: -0.2
            opacity: Theme.night ? 0.45 : 0.3
        }
        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                GradientStop { position: 0; color: Qt.alpha(Theme.layerSolid, 0.25) }
                GradientStop { position: 0.7; color: Qt.alpha(Theme.layerSolid, 0.85) }
                GradientStop { position: 1; color: Theme.layerSolid }
            }
        }
    }

    GridLayout {
        id: body
        width: page.usable
        x: (page.width - width) / 2
        y: page.wide ? 120 : 96
        columns: page.wide ? 2 : 1
        columnSpacing: Theme.s8
        rowSpacing: Theme.s7

        // the summary card's place; on a wide window the card stays in
        // view while the page scrolls
        Item {
            id: summarySlot
            Layout.preferredWidth: page.wide ? 340 : body.width
            Layout.preferredHeight: summary.implicitHeight
            Layout.alignment: Qt.AlignTop
        }

        // --- everything else, in the page's column ---------------------
        ColumnLayout {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignTop
            Layout.topMargin: page.wide ? Theme.s3 : 0
            spacing: Theme.sectionGap

            // screenshots
            ColumnLayout {
                Layout.fillWidth: true
                visible: page.shots.length > 0
                spacing: Theme.s4
                SectionHeader { Layout.fillWidth: true; title: i18nc("@title", "Screenshots") }
                Item {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 220
                    HoverHandler { id: stripHover }
                    ListView {
                        id: strip
                        anchors.fill: parent
                        orientation: ListView.Horizontal
                        spacing: Theme.rowSpacing
                        interactive: false
                        clip: true
                        model: page.shots
                        Behavior on contentX { NumberAnimation { duration: Theme.durationSlow; easing.type: Theme.easingEnter } }
                        delegate: ScreenshotThumb {
                            required property var modelData
                            required property int index
                            height: strip.height
                            source: modelData.thumb || modelData.full
                            caption: modelData.caption || ""
                            onClicked: viewer.show(index)
                        }
                    }
                    RoundArrow {
                        glyph: "chevron-left"
                        anchors.left: parent.left
                        anchors.leftMargin: Theme.s3
                        anchors.verticalCenter: parent.verticalCenter
                        shown: stripHover.hovered && strip.contentX > 1
                        Accessible.name: i18nc("@action:button screenshots", "Previous")
                        onClicked: strip.contentX = Math.max(0, strip.contentX - strip.width * 0.8)
                    }
                    RoundArrow {
                        glyph: "chevron-right"
                        anchors.right: parent.right
                        anchors.rightMargin: Theme.s3
                        anchors.verticalCenter: parent.verticalCenter
                        shown: stripHover.hovered && strip.contentX + strip.width < strip.contentWidth - 1
                        Accessible.name: i18nc("@action:button screenshots", "Next")
                        onClicked: strip.contentX = Math.min(strip.contentWidth - strip.width, strip.contentX + strip.width * 0.8)
                    }
                }
            }

            // description, folded after a few lines
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.s4
                SectionHeader { Layout.fillWidth: true; title: i18nc("@title", "Description") }
                Text {
                    Layout.fillWidth: true
                    text: page.app ? page.app.summary : ""
                    font: Theme.bodyStrong
                    color: Theme.text
                    wrapMode: Text.Wrap
                }
                Item {
                    Layout.fillWidth: true
                    implicitHeight: page.descriptionExpanded || !description.folds ? description.implicitHeight : description.foldHeight
                    clip: true
                    Behavior on implicitHeight { NumberAnimation { duration: Theme.durationNormal; easing.type: Theme.easingEnter } }
                    Text {
                        id: description
                        readonly property real foldHeight: Theme.bodyLineHeight * 7
                        readonly property bool folds: implicitHeight > foldHeight + Theme.bodyLineHeight
                        width: parent.width
                        text: page.app ? page.app.description : ""
                        textFormat: Text.StyledText
                        font: Theme.body
                        color: Theme.textSecondary
                        wrapMode: Text.Wrap
                        lineHeightMode: Text.FixedHeight
                        lineHeight: Theme.bodyLineHeight
                    }
                    Rectangle {
                        visible: description.folds && !page.descriptionExpanded
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: Theme.bodyLineHeight * 2
                        gradient: Gradient {
                            GradientStop { position: 0; color: Qt.alpha(Theme.layerSolid, 0) }
                            GradientStop { position: 1; color: Theme.layerSolid }
                        }
                    }
                }
                Button {
                    visible: description.folds
                    subtle: true
                    leftPadding: Theme.s2
                    text: page.descriptionExpanded ? i18nc("@action:button", "Show less") : i18nc("@action:button", "Show more")
                    textColor: Theme.accentText
                    onClicked: page.descriptionExpanded = !page.descriptionExpanded
                }
            }

            // ratings
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.s4
                visible: page.app !== null
                SectionHeader { Layout.fillWidth: true; title: i18nc("@title", "Ratings") }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.s8
                    visible: Format.hasRating(page.app)
                    ColumnLayout {
                        spacing: Theme.s1
                        Text {
                            text: Format.hasRating(page.app) ? Format.rating(page.app.ratingAvg) : ""
                            font: Theme.titleLarge
                            color: Theme.text
                        }
                        Stars { value: Format.hasRating(page.app) ? page.app.ratingAvg : 0; size: 16 }
                        Text {
                            Layout.topMargin: Theme.s2
                            text: page.app ? i18ncp("@info", "One rating", "%1 ratings", page.app.ratingCount) : ""
                            font: Theme.caption
                            color: Theme.textSecondary
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.maximumWidth: 420
                        spacing: Theme.s2
                        Repeater {
                            model: 5
                            delegate: RowLayout {
                                required property int index
                                readonly property int stars: 5 - index
                                readonly property var histogram: page.app && page.app.ratingHistogram ? page.app.ratingHistogram : [0, 0, 0, 0, 0]
                                readonly property int most: Math.max(1, Math.max.apply(null, histogram))
                                Layout.fillWidth: true
                                spacing: Theme.s3
                                Text {
                                    text: stars
                                    font: Theme.caption
                                    color: Theme.textSecondary
                                    Layout.preferredWidth: 8
                                }
                                Glyph { name: "star"; size: 10; color: Theme.textSecondary }
                                Rectangle {
                                    Layout.fillWidth: true
                                    implicitHeight: 4
                                    radius: 2
                                    color: Theme.strokeSurface
                                    Rectangle {
                                        height: parent.height
                                        radius: 2
                                        width: parent.width * (histogram[stars - 1] || 0) / most
                                        color: Theme.accent
                                    }
                                }
                            }
                        }
                    }
                }
                Text {
                    visible: !Format.hasRating(page.app)
                    text: i18n("Not enough people have rated this app yet.")
                    font: Theme.body
                    color: Theme.textSecondary
                }
            }

            // what's new
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.s3
                visible: page.release !== null && (page.release.version || page.release.notes)
                SectionHeader { Layout.fillWidth: true; title: i18nc("@title", "What's new") }
                Text {
                    text: page.release ? [page.release.version ? i18nc("@info", "Version %1", page.release.version) : "",
                                          Format.date(page.release.date)].filter(s => s !== "").join("  ·  ") : ""
                    font: Theme.captionStrong
                    color: Theme.textSecondary
                }
                Text {
                    Layout.fillWidth: true
                    visible: text !== ""
                    text: page.release && page.release.notes ? page.release.notes : ""
                    textFormat: Text.StyledText
                    font: Theme.body
                    color: Theme.textSecondary
                    wrapMode: Text.Wrap
                    lineHeightMode: Text.FixedHeight
                    lineHeight: Theme.bodyLineHeight
                    maximumLineCount: 10
                    elide: Text.ElideRight
                }
            }

            // details
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.s4
                visible: page.app !== null
                SectionHeader { Layout.fillWidth: true; title: i18nc("@title", "Additional information") }
                GridLayout {
                    Layout.fillWidth: true
                    columns: Math.max(1, Math.floor(width / 220))
                    columnSpacing: Theme.s7
                    rowSpacing: Theme.s5
                    Repeater {
                        model: page.details()
                        delegate: ColumnLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignTop
                            Layout.preferredWidth: 200
                            spacing: 2
                            Text {
                                text: modelData.label
                                font: Theme.captionStrong
                                color: Theme.text
                            }
                            Text {
                                Layout.fillWidth: true
                                text: modelData.link ? "<a href=\"" + modelData.link + "\">" + modelData.value + "</a>" : modelData.value
                                textFormat: modelData.link ? Text.StyledText : Text.PlainText
                                linkColor: Theme.accentText
                                font: Theme.body
                                color: Theme.textSecondary
                                wrapMode: Text.Wrap
                                onLinkActivated: link => Qt.openUrlExternally(link)
                                HoverHandler { cursorShape: modelData.link ? Qt.PointingHandCursor : Qt.ArrowCursor }
                            }
                        }
                    }
                }
            }

            CardRow {
                Layout.fillWidth: true
                title: i18nc("@title row of apps", "People also like")
                model: alsoLike
                skipId: page.app ? page.app.appId : ""
                visible: count > 1
                showAll: false
                onOpen: other => page.open(other)
            }
        }
    }

    // --- the summary card ----------------------------------------------
    Rectangle {
        id: summary
        readonly property real rest: body.y + summarySlot.y
        x: body.x + summarySlot.x
        width: summarySlot.width
        y: page.wide ? rest + Math.max(0, Math.min(page.contentY + Theme.s7 - rest, body.height - implicitHeight - summarySlot.y)) : rest
        implicitHeight: summaryColumn.implicitHeight + 2 * Theme.s7
        radius: Theme.radiusCard + 2
        color: Theme.cardSolid
        border.width: 1
        border.color: Theme.strokeSurface

        ColumnLayout {
            id: summaryColumn
            anchors.fill: parent
            anchors.margins: Theme.s7
            spacing: Theme.s3

            Kirigami.Icon {
                source: page.app ? (page.app.iconSource || "application-x-executable") : ""
                fallback: "application-x-executable"
                Layout.preferredWidth: 112
                Layout.preferredHeight: 112
                Layout.bottomMargin: Theme.s3
            }
            Text {
                Layout.fillWidth: true
                text: page.app ? page.app.name : ""
                font: Theme.title
                color: Theme.text
                wrapMode: Text.Wrap
                Accessible.role: Accessible.Heading
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.s2
                visible: maker.text !== ""
                Text {
                    id: maker
                    Layout.fillWidth: page.app && !page.app.verified
                    text: page.app ? page.app.developer : ""
                    font: Theme.body
                    color: homepage !== "" ? Theme.accentText : Theme.textSecondary
                    elide: Text.ElideRight
                    readonly property string homepage: page.app && page.app.urls ? page.app.urls.homepage || "" : ""
                    HoverHandler { cursorShape: maker.homepage !== "" ? Qt.PointingHandCursor : Qt.ArrowCursor }
                    TapHandler { onTapped: if (maker.homepage !== "") Qt.openUrlExternally(maker.homepage) }
                }
                Glyph {
                    visible: page.app && page.app.verified === true
                    name: "verified"
                    size: 16
                    color: Theme.accentText
                    QQC2.ToolTip.visible: verifiedHover.hovered
                    QQC2.ToolTip.text: i18nc("@info:tooltip", "The maker checked this app's page on Flathub")
                    HoverHandler { id: verifiedHover }
                }
                Item { Layout.fillWidth: true; visible: page.app && page.app.verified === true }
            }

            // rating, category, age
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: Theme.s2
                spacing: Theme.s3
                Stars {
                    visible: Format.hasRating(page.app)
                    value: visible ? page.app.ratingAvg : 0
                    size: 13
                }
                Text {
                    text: Format.hasRating(page.app)
                          ? i18nc("@info 4.5 · 1.2K ratings", "%1 · %2 ratings", Format.rating(page.app.ratingAvg), Format.count(page.app.ratingCount))
                          : i18n("Not enough ratings yet")
                    font: Theme.caption
                    color: Theme.textSecondary
                }
            }
            Flow {
                Layout.fillWidth: true
                spacing: Theme.s2
                Tag { text: page.app ? Format.categoryName(page.app.categories) : ""; visible: text !== "" }
                Tag {
                    visible: page.app && page.app.source !== "windows"
                    text: page.app && page.app.minimumAge > 0 ? i18nc("@info age rating", "Ages %1+", page.app.minimumAge)
                                                              : i18nc("@info age rating", "Everyone")
                }
                Tag {
                    visible: page.app && page.app.preview === true
                    text: i18nc("@info an early version", "Preview")
                    tint: Theme.caution
                }
                Tag {
                    visible: text !== ""
                    text: Format.windowsLabel(page.app)
                    tint: Format.windowsTint(page.app)
                }
            }

            // Install / Open / Uninstall, or how far the install is
            Item { Layout.preferredHeight: Theme.s3 }
            RowLayout {
                Layout.fillWidth: true
                visible: !page.working && !page.blocked
                spacing: Theme.s3
                Button {
                    id: installButton
                    Layout.fillWidth: true
                    implicitHeight: 40
                    visible: !page.installed && !page.blocked
                    accent: true
                    text: i18n("Install")
                    onClicked: installer.get(page.app)
                }
                Button {
                    Layout.fillWidth: true
                    implicitHeight: 40
                    visible: page.installed
                    accent: true
                    text: i18n("Open")
                    onClicked: installer.open(page.app)
                }
                Button {
                    Layout.fillWidth: true
                    implicitHeight: 40
                    visible: page.installed && !page.systemCopy
                    text: i18n("Uninstall")
                    onClicked: confirmUninstall.ask(page.app)
                }
            }
            // a Windows setup that runs out of sight accepts its maker's
            // terms for the person: show them before Install, not after
            Text {
                readonly property string terms: page.app && page.app.source === "windows" && page.app.urls
                                                ? page.app.urls.license || "" : ""
                Layout.fillWidth: true
                visible: terms !== "" && !page.installed && !page.working && !page.blocked
                text: i18n("Installing means you accept its <a href=\"%1\">license terms</a>.", terms)
                textFormat: Text.StyledText
                linkColor: Theme.accentText
                font: Theme.caption
                color: Theme.textSecondary
                wrapMode: Text.Wrap
                onLinkActivated: link => Qt.openUrlExternally(link)
                HoverHandler { cursorShape: parent.hoveredLink !== "" ? Qt.PointingHandCursor : Qt.ArrowCursor }
            }
            // Microsoft Store style: the bar under the app, with Cancel
            JobStatus {
                Layout.fillWidth: true
                job: page.job
                onCancel: installer.cancelJob(page.job.jobId)
            }
            InfoBar {
                Layout.fillWidth: true
                visible: page.job.state === 6 // Failed
                text: page.job.reasonText || ""
                onClosed: if (page.app) installer.dismiss(page.app.ref)
            }
            // what Raye knows about a Windows program, before anything downloads
            Rectangle {
                Layout.fillWidth: true
                visible: windowsText.text !== ""
                implicitHeight: windowsColumn.implicitHeight + 2 * Theme.s4
                radius: Theme.radiusCard - 2
                color: Qt.alpha(Format.windowsTint(page.app), Theme.night ? 0.14 : 0.08)
                border.width: 1
                border.color: Qt.alpha(Format.windowsTint(page.app), 0.35)
                ColumnLayout {
                    id: windowsColumn
                    anchors.fill: parent
                    anchors.margins: Theme.s4
                    spacing: Theme.s2
                    Text {
                        Layout.fillWidth: true
                        text: Format.windowsLabel(page.app)
                        font: Theme.bodyStrong
                        color: Theme.text
                    }
                    Text {
                        id: windowsText
                        Layout.fillWidth: true
                        text: Format.windowsExplained(page.app)
                        font: Theme.caption
                        color: Theme.textSecondary
                        wrapMode: Text.Wrap
                    }
                    Text {
                        Layout.fillWidth: true
                        // the summary of a game that can't run already says why
                        visible: text !== "" && !page.blocked
                        text: page.app && page.app.windowsNote ? page.app.windowsNote : ""
                        font: Theme.caption
                        color: Theme.text
                        wrapMode: Text.Wrap
                    }
                }
            }
            // a launcher Raye sets up, or an app that does the job of a
            // Windows program with no version here
            Rectangle {
                Layout.fillWidth: true
                visible: insteadTitle.text !== "" || launcherText.text !== ""
                implicitHeight: insteadColumn.implicitHeight + 2 * Theme.s4
                radius: Theme.radiusCard - 2
                color: Qt.alpha(Theme.accentText, Theme.night ? 0.12 : 0.07)
                border.width: 1
                border.color: Qt.alpha(Theme.accentText, 0.3)
                ColumnLayout {
                    id: insteadColumn
                    anchors.fill: parent
                    anchors.margins: Theme.s4
                    spacing: Theme.s2
                    Text {
                        id: insteadTitle
                        Layout.fillWidth: true
                        visible: text !== ""
                        text: Format.replacesText(page.app)
                        font: Theme.bodyStrong
                        color: Theme.text
                        wrapMode: Text.Wrap
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: insteadTitle.text !== ""
                        text: Format.replacesExplained(page.app)
                        font: Theme.caption
                        color: Theme.textSecondary
                        wrapMode: Text.Wrap
                    }
                    Text {
                        id: launcherText
                        Layout.fillWidth: true
                        visible: text !== ""
                        text: Format.launcherNote(page.app)
                        font: Theme.caption
                        color: Theme.text
                        wrapMode: Text.Wrap
                    }
                }
            }
            Text {
                Layout.fillWidth: true
                visible: page.app && page.app.preview === true
                text: i18n("Preview: an early version that is still being written. Expect missing features.")
                font: Theme.caption
                color: Theme.textSecondary
                wrapMode: Text.Wrap
            }
            Text {
                Layout.fillWidth: true
                Layout.topMargin: Theme.s2
                visible: !page.blocked
                text: page.systemCopy ? i18n("Installed on this PC · system")
                      : page.app && page.app.source === "windows"
                        ? (page.app.downloadSize > 0 ? i18n("Its maker's own installer, checked by Raye · %1", Format.size(page.app.downloadSize))
                                                     : i18n("Its maker's own installer, checked by Raye"))
                      : page.app ? (page.app.source === "flatpak" ? i18n("Free · from Flathub")
                                  : page.app.source === "raye" ? i18n("Free · offered by Raye, from its makers")
                                  : i18n("Free · a Raye Linux package")) : ""
                font: Theme.caption
                color: Theme.textTertiary
                wrapMode: Text.Wrap
            }
        }
    }

    function details() {
        if (!app)
            return []
        const out = []
        const add = (label, value, link) => { if (value) out.push({ label: label, value: value, link: link || "" }) }
        add(i18nc("@label", "Made by"), app.developer)
        const proprietary = (app.license || "").indexOf("LicenseRef-proprietary") >= 0
        add(i18nc("@label", "License"), proprietary ? i18nc("@info license", "Proprietary")
            : app.isFree ? i18nc("@info license: %1 is its name", "Free and open source (%1)", app.license) : app.license)
        add(i18nc("@label", "Version"), release ? release.version : "")
        add(i18nc("@label", "Last update"), Format.date(app.updatedAt || (release ? release.date : null)))
        add(i18nc("@label", "Category"), Format.categoryName(app.categories))
        // nobody rated a Windows program's age for Raye: say nothing rather than "Everyone"
        if (app.source !== "windows")
            add(i18nc("@label", "Age rating"), app.minimumAge > 0 ? i18nc("@info age rating", "Ages %1+", app.minimumAge)
                                                             : i18nc("@info age rating", "Everyone"))
        if (app.popularity > 0 && app.source === "flatpak")
            add(i18nc("@label", "Installs last month"), Format.count(app.popularity))
        if (app.windowsState !== "blocked")
            add(i18nc("@label", "Comes from"), Format.source(app))
        const urls = app.urls || {}
        add(i18nc("@label", "Website"), urls.homepage ? urls.homepage.replace(/^https?:\/\//, "").replace(/\/$/, "") : "", urls.homepage)
        add(i18nc("@label", "Help"), urls.help ? i18nc("@info link", "Get help") : "", urls.help)
        add(i18nc("@label", "Problems"), urls.bugtracker ? i18nc("@info link", "Report a problem") : "", urls.bugtracker)
        return out
    }

    ScreenshotViewer {
        id: viewer
        shots: page.shots
    }

    UninstallDialog { id: confirmUninstall }

    component Tag: Rectangle {
        property alias text: tagLabel.text
        property color tint: Theme.textSecondary
        implicitWidth: tagLabel.implicitWidth + 2 * Theme.s3
        implicitHeight: 22
        radius: 11
        color: Qt.alpha(tint, 0.12)
        Text {
            id: tagLabel
            anchors.centerIn: parent
            font: Theme.caption
            color: parent.tint === Theme.textSecondary ? Theme.text : parent.tint
        }
    }
}
