// SPDX-License-Identifier: GPL-3.0-or-later
// The big carousel at the top of Home, Apps and Games. Each slide is
// drawn here from the app itself: its colour, icon, name and first
// screenshot, nothing made by hand for it. Moves on every 7 seconds
// and stays put while the pointer or the keyboard is on it.
import QtQuick
import QtQuick.Window
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

FocusScope {
    id: hero

    // maps from catalog.find(), each with a screenshot
    property var apps: []
    property bool loading: false
    property int current: 0
    signal open(var app)

    readonly property bool narrow: width < 900
    // what the screenshots are decoded for. They follow the size only once
    // a resize stops, in 40 px steps: decoding them again on every pixel
    // of a drag kept the window from following and could crash it
    property int decodeHeight: 0
    property bool wideBack: false
    function settleSize() {
        if (height <= 0)
            return
        decodeHeight = Math.ceil(height / 40) * 40
        wideBack = width > 1200
    }
    onHeightChanged: decodeHeight > 0 ? sizeSettle.restart() : settleSize()
    onWidthChanged: decodeHeight > 0 ? sizeSettle.restart() : settleSize()
    Timer { id: sizeSettle; interval: 300; onTriggered: hero.settleSize() }
    implicitHeight: narrow ? 300 : Math.round(Math.max(360, Math.min(440, width * 0.27)))
    activeFocusOnTab: apps.length > 1
    Accessible.role: Accessible.Grouping
    Accessible.name: i18nc("@title the featured apps at the top", "Featured")

    onAppsChanged: if (current >= apps.length) current = 0
    function step(by) {
        if (apps.length > 0)
            current = (current + by + apps.length) % apps.length
    }
    Keys.onLeftPressed: step(-1)
    Keys.onRightPressed: step(1)

    HoverHandler { id: hover }
    // moves on while someone can see it, once round the slides; then it
    // rests for good, so an idle Store does nothing (the arrows and pips
    // still work)
    property int autoSteps: 0
    Timer {
        interval: 7000
        repeat: true
        running: hero.visible && hero.apps.length > 1 && hero.autoSteps < hero.apps.length
                 && !hover.hovered && !hero.activeFocus
                 && Qt.application.state === Qt.ApplicationActive && !Theme.animationsOff
        onTriggered: {
            hero.autoSteps++
            hero.step(1)
        }
    }

    // while the catalog loads
    Rectangle {
        anchors.fill: parent
        visible: hero.apps.length === 0 && hero.loading
        radius: Theme.radiusHero
        color: Theme.cardSolid
        Shimmer { anchors.fill: parent; radius: Theme.radiusHero; color: "transparent" }
    }

    Repeater {
        model: hero.apps
        delegate: Slide {
            required property var modelData
            required property int index
            app: modelData
            width: hero.width
            height: hero.height
            shown: index === hero.current
            // pictures for the slide on show and the next one only
            wanted: shown || index === (hero.current + 1) % hero.apps.length
        }
    }

    RoundArrow {
        z: 2
        glyph: "chevron-left"
        anchors.left: parent.left
        anchors.leftMargin: Theme.s4
        anchors.verticalCenter: parent.verticalCenter
        shown: hover.hovered && hero.apps.length > 1
        Accessible.name: i18nc("@action:button featured apps", "Previous")
        onClicked: hero.step(-1)
    }
    RoundArrow {
        z: 2
        glyph: "chevron-right"
        anchors.right: parent.right
        anchors.rightMargin: Theme.s4
        anchors.verticalCenter: parent.verticalCenter
        shown: hover.hovered && hero.apps.length > 1
        Accessible.name: i18nc("@action:button featured apps", "Next")
        onClicked: hero.step(1)
    }

    // pips: a dot per slide, the current one a short bar
    Row {
        z: 2
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.s5
        anchors.horizontalCenter: parent.horizontalCenter
        spacing: 6
        visible: hero.apps.length > 1
        Repeater {
            model: hero.apps.length
            delegate: Rectangle {
                required property int index
                width: index === hero.current ? 20 : 6
                height: 6
                radius: 3
                color: index === hero.current ? "#ffffff" : "#66ffffff"
                Behavior on width { NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easingEnter } }
                TapHandler { onTapped: hero.current = index }
            }
        }
    }

    // keyboard focus on the carousel itself
    FocusRing { z: 2; shown: hero.activeFocus; radius: Theme.radiusHero }

    component Slide: Item {
        id: slide
        property var app
        property bool shown: false
        property bool wanted: false
        // once loaded, a slide keeps its pictures
        property bool loaded: false
        onWantedChanged: if (wanted) loaded = true
        Component.onCompleted: if (wanted) loaded = true
        readonly property color brand: Format.brand(app)

        opacity: shown ? 1 : 0
        visible: opacity > 0
        z: shown ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: Theme.durationSlow * 1.2; easing.type: Easing.InOutQuad } }

        TapHandler { onTapped: hero.open(slide.app) }

        // the backdrop: the app's colour fading into Raye Night
        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusHero
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0; color: Theme.mix("#141318", slide.brand, Theme.night ? 0.55 : 0.75) }
                GradientStop { position: 0.55; color: Theme.mix("#141318", slide.brand, 0.22) }
                GradientStop { position: 1; color: "#141318" }
            }
            border.width: 1
            border.color: "#1affffff"
        }
        // darker towards the bottom, so the pips read
        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusHero
            gradient: Gradient {
                GradientStop { position: 0.55; color: "transparent" }
                GradientStop { position: 1; color: "#59000000" }
            }
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: hero.narrow ? 32 : 56
            anchors.rightMargin: 40
            spacing: 40

            ColumnLayout {
                Layout.fillWidth: true
                Layout.maximumWidth: 460
                Layout.alignment: Qt.AlignVCenter
                spacing: Theme.s3

                Kirigami.Icon {
                    source: slide.app ? (slide.app.iconSource || "application-x-executable") : ""
                    fallback: "application-x-executable"
                    Layout.preferredWidth: hero.narrow ? 64 : 80
                    Layout.preferredHeight: hero.narrow ? 64 : 80
                    Layout.bottomMargin: Theme.s3
                }
                Text {
                    Layout.fillWidth: true
                    text: slide.app ? slide.app.name : ""
                    font: hero.narrow ? Theme.title : Theme.titleLarge
                    color: "#ffffff"
                    elide: Text.ElideRight
                }
                Text {
                    Layout.fillWidth: true
                    text: slide.app ? slide.app.summary : ""
                    font: Theme.bodyLarge
                    color: "#d9ffffff"
                    wrapMode: Text.Wrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                }
                RowLayout {
                    Layout.topMargin: Theme.s4
                    spacing: Theme.s5
                    GetButton {
                        app: slide.app
                        implicitHeight: 36
                        implicitWidth: Math.max(132, implicitContentWidth + leftPadding + rightPadding)
                    }
                    Row {
                        spacing: Theme.s2
                        visible: Format.hasRating(slide.app)
                        Glyph { name: "star"; size: 13; color: "#ffffff"; anchors.verticalCenter: parent.verticalCenter }
                        Text {
                            text: Format.hasRating(slide.app) ? Format.rating(slide.app.ratingAvg) : ""
                            font: Theme.bodyStrong
                            color: "#ffffff"
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }
                    Text {
                        text: slide.app ? Format.categoryName(slide.app.categories) : ""
                        font: Theme.body
                        color: "#b3ffffff"
                    }
                }
            }

            // the app's first screenshot, framed, at the right; on a wide
            // hero the second one stands behind it
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.topMargin: 36
                Layout.bottomMargin: 36
                visible: !hero.narrow || hero.width > 760
                // one HeroShot per picture and size: a new one replaces it
                // instead of its texture being swapped under the renderer,
                // which crashed the Store while it loaded or was resized
                Item {
                    property Item shot: null
                    readonly property real ratio: shot && shot.ready ? shot.ratio : 16 / 10
                    height: frameBox.height * 0.82
                    width: height * ratio
                    anchors.right: frameBox.left
                    anchors.rightMargin: -width * 0.38
                    anchors.verticalCenter: frameBox.verticalCenter
                    Repeater {
                        model: hero.wideBack && hero.decodeHeight > 0 && slide.loaded && slide.app && slide.app.screenshots
                               && slide.app.screenshots.length > 1
                               ? [{ url: slide.app.screenshots[1].thumb || slide.app.screenshots[1].full,
                                    height: Math.ceil(hero.decodeHeight * 0.82) }] : []
                        onItemAdded: (index, item) => parent.shot = item
                        delegate: HeroShot {
                            required property var modelData
                            url: modelData.url
                            decodeHeight: modelData.height
                            borderColor: "#1affffff"
                            shadowSize: 28
                            shadowOffset: 8
                            shadowColor: "#66000000"
                            shownOpacity: 0.6
                        }
                    }
                }
                Item {
                    id: frameBox
                    property Item shot: null
                    readonly property real ratio: shot && shot.ready ? shot.ratio : 16 / 10
                    height: Math.min(parent.height, parent.width / ratio)
                    width: height * ratio
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    Repeater {
                        model: hero.decodeHeight > 0 && slide.loaded && slide.app && slide.app.screenshots
                               && slide.app.screenshots.length > 0
                               ? [{ url: Screen.devicePixelRatio > 1.25 ? slide.app.screenshots[0].full || slide.app.screenshots[0].thumb
                                                                        : slide.app.screenshots[0].thumb || slide.app.screenshots[0].full,
                                    height: hero.decodeHeight - 72 }] : []
                        onItemAdded: (index, item) => frameBox.shot = item
                        delegate: HeroShot {
                            required property var modelData
                            url: modelData.url
                            decodeHeight: modelData.height
                            borderColor: "#26ffffff"
                            shadowSize: 36
                            shadowOffset: 10
                            shadowColor: "#73000000"
                        }
                    }
                }
            }
        }
    }
}
