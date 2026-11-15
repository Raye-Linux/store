// SPDX-License-Identifier: GPL-3.0-or-later
// The look of the Store in one place: colours, type, sizes and motion.
// Every component under ui/ and every page reads from here and never
// writes a colour of its own.
//
// Colours are the Raye tokens (theme/tokens/raye-tokens.json, the same
// values Raye Settings uses): Night or Day follows the colour scheme in
// use and the accent follows the user's accent colour. Values with alpha
// are drawn over the window; the *Solid ones are what they look like on
// an opaque window. Durations are the Fluent ones, scaled by the user's
// animation speed and 0 when animations are off.
pragma Singleton

import QtQuick
import org.kde.kirigami as Kirigami

QtObject {
    id: theme

    // The colour scheme of the application window. Kirigami.Theme sits on
    // this object itself: read through a child item it answers black.
    Kirigami.Theme.colorSet: Kirigami.Theme.Window
    Kirigami.Theme.inherit: false

    readonly property var _scheme: readScheme(Kirigami.Theme.backgroundColor,
                                              Kirigami.Theme.textColor,
                                              Kirigami.Theme.highlightColor)
    // "night" or "day" pins the mode, "" follows the colour scheme
    property string forceMode: ""
    readonly property bool night: forceMode === "night" ? true
                                : forceMode === "day" ? false : _scheme.night
    readonly property bool day: !night

    // --- accent -------------------------------------------------------
    readonly property color accent: _scheme.accent
    readonly property bool defaultAccent: Qt.colorEqual(accent, "#7b3cff")
    readonly property var _accent: defaultAccent ? (night ? _nightAccent : _dayAccent)
                                                 : deriveAccent(accent, night)
    readonly property var _nightAccent: ({ hover: "#8950ff", pressed: "#6a2fe6", text: "#b48cff", disabled: "#3a2a5e", onAccent: "#ffffff" })
    readonly property var _dayAccent: ({ hover: "#6c2ff0", pressed: "#5f27d6", text: "#6a2fe6", disabled: "#c9b8f2", onAccent: "#ffffff" })
    readonly property color accentHover: _accent.hover
    readonly property color accentPressed: _accent.pressed
    // links and accent text on the page
    readonly property color accentText: _accent.text
    readonly property color accentDisabled: _accent.disabled
    readonly property color textOnAccent: _accent.onAccent
    // The far end of the accent gradient (nav pill, toggles, focus glow):
    // the accent itself, so those read as one flat colour
    readonly property color accentGlowEnd: accent
    // the soft light around focused and primary things
    readonly property color glow: Qt.alpha(accent, night ? 0.22 : 0.14)

    // --- surfaces ------------------------------------------------------
    // The window: bg.base, see-through over the blurred wallpaper when
    // the window gets blur (90 % Night, 88 % Day), opaque otherwise.
    readonly property color backdrop: night ? "#e6141318" : "#e0f5f4f7"
    readonly property color backdropSolid: night ? "#141318" : "#f5f4f7"
    // nav pane and side areas
    readonly property color paneSolid: night ? "#100f13" : "#eeedf1"
    // the content area beside the nav pane (bg.layer)
    readonly property color layer: night ? "#08ffffff" : "#99ffffff"
    readonly property color layerSolid: night ? "#1b1a1f" : "#fbfbfc"
    // settings cards (bg.card, bg.card.hover; pressed one step back)
    readonly property color card: night ? "#0dffffff" : "#d9ffffff"
    readonly property color cardSolid: night ? "#212026" : "#ffffff"
    readonly property color cardHover: night ? "#14ffffff" : "#f7f7f9"
    readonly property color cardSolidHover: night ? "#28272d" : "#f7f7f9"
    readonly property color cardPressed: night ? "#09ffffff" : "#f2f1f4"
    readonly property color cardBorder: night ? "#33000000" : "#1217151c"
    // the 1 px lit top edge of a card, Night only (Day has its shadow)
    readonly property color cardTopEdge: night ? "#0fffffff" : "transparent"
    // popups, combo lists, dialogs: the highest solid layer
    readonly property color flyout: night ? "#211f26" : "#ffffff"
    readonly property color flyoutFooter: night ? "#1a191f" : "#f5f4f7"
    // behind a dialog
    readonly property color smoke: night ? "#66000000" : "#4c17151c"

    // --- controls ------------------------------------------------------
    readonly property color control: night ? "#12ffffff" : "#d9ffffff"
    readonly property color controlHover: night ? "#1affffff" : "#f7f7f9"
    readonly property color controlPressed: night ? "#0affffff" : "#f2f1f4"
    readonly property color controlDisabled: night ? "#1c1b20" : "#f2f1f4"
    readonly property color inputActive: night ? "#100f13" : "#ffffff"
    readonly property color subtleHover: night ? "#0fffffff" : "#0a17151c"
    readonly property color subtlePressed: night ? "#0affffff" : "#0617151c"
    readonly property color subtleSelected: night ? "#17ffffff" : "#0f17151c"
    readonly property color strokeControl: night ? "#12ffffff" : "#1417151c"
    readonly property color strokeControlBottom: night ? "#0affffff" : "#2917151c"
    // checkbox, radio, toggle and slider rails at rest: 3:1 on any layer
    readonly property color strokeStrong: night ? "#8b8893" : "#85828d"
    readonly property color strokeSurface: night ? "#17ffffff" : "#1a17151c"
    readonly property color divider: night ? "#14ffffff" : "#1417151c"

    // --- text ----------------------------------------------------------
    readonly property color text: night ? "#f1f0f4" : "#17151c"
    readonly property color textSecondary: night ? "#b5b3bc" : "#54525b"
    readonly property color textTertiary: night ? "#97949f" : "#6c6975"
    readonly property color textDisabled: night ? "#5d5b64" : "#aaa8b0"

    // --- focus (keyboard only) -----------------------------------------
    readonly property color focusOuter: night ? "#f1f0f4" : "#17151c"
    readonly property color focusInner: night ? "#b2000000" : "#b2ffffff"

    // --- status ----------------------------------------------------------
    readonly property color critical: night ? "#ff6b85" : "#c4264b"
    readonly property color caution: night ? "#ffc857" : "#9a6200"
    readonly property color success: night ? "#5fe0a1" : "#0e7c48"
    readonly property color info: night ? "#8fb8ff" : "#2f5fc9"
    readonly property color shadow: night ? "#000000" : "#17151c"

    // --- brand, the same in both modes ----------------------------------
    readonly property color brandViolet: "#7b3cff"
    readonly property color brandMagenta: "#c040ff"

    // --- type (Inter; Raye's body is 10 pt, Segoe's 14 px optically) ----
    readonly property font caption: Qt.font({ family: "Inter", pointSize: 9, weight: Font.Normal })
    readonly property font captionStrong: Qt.font({ family: "Inter", pointSize: 9, weight: Font.DemiBold })
    readonly property font body: Qt.font({ family: "Inter", pointSize: 10, weight: Font.Normal })
    readonly property font bodyStrong: Qt.font({ family: "Inter", pointSize: 10, weight: Font.DemiBold })
    readonly property font bodyLarge: Qt.font({ family: "Inter", pointSize: 12.75, weight: Font.Normal })
    readonly property font tileTitle: Qt.font({ family: "Inter", pointSize: 11.25, weight: Font.DemiBold })
    readonly property font subtitle: Qt.font({ family: "Inter", pointSize: 14.25, weight: Font.DemiBold })
    readonly property font title: Qt.font({ family: "Inter Display", pointSize: 20.25, weight: Font.DemiBold })
    readonly property font titleLarge: Qt.font({ family: "Inter Display", pointSize: 28.5, weight: Font.DemiBold })
    readonly property int captionLineHeight: 16
    readonly property int bodyLineHeight: 20
    readonly property int subtitleLineHeight: 26
    readonly property int titleLineHeight: 34

    // --- geometry ------------------------------------------------------
    readonly property int s1: 2
    readonly property int s2: 4
    readonly property int s3: 8
    readonly property int s4: 12
    readonly property int s5: 16
    readonly property int s6: 20
    readonly property int s7: 24
    readonly property int s8: 32

    readonly property int s9: 40
    readonly property int s10: 48

    readonly property real radiusControl: 6        // buttons, fields, chips' inner shapes
    readonly property real radiusCard: 8           // cards, tiles, screenshots
    readonly property real radiusHero: 12          // the hero and big artwork
    readonly property real radiusSmall: 2          // progress, rating bars

    readonly property int controlHeight: 32
    readonly property int controlMinWidth: 120     // Get, Open, Install
    readonly property int headerHeight: 52         // the bar with the search box
    readonly property int searchMaxWidth: 560
    readonly property int railWidth: 88
    readonly property int railItemWidth: 78
    readonly property int railItemHeight: 58
    readonly property int railIcon: 22
    readonly property int pillWidth: 3
    readonly property int pillHeight: 16
    readonly property int progressHeight: 4
    readonly property int contentPadding: 32       // page edges next to the rail
    readonly property int contentMaxWidth: 1600
    readonly property int cardWidth: 168           // an app in a row
    readonly property int cardHeight: 218
    readonly property int cardTile: 104            // the tinted square behind its icon
    readonly property int cardIcon: 64
    readonly property int rowSpacing: 12           // between cards
    readonly property int sectionGap: 36           // between rows of a page
    readonly property int sectionHeight: 270       // a row of cards with its title
    readonly property int listRowHeight: 64        // Library and Downloads
    readonly property int focusRingOffset: 3       // outer 2 + inner 1, outside the control

    // --- motion ------------------------------------------------------
    // Plasma's animation speed (kdeglobals AnimationDurationFactor) as
    // Kirigami reads it: 200 ms at speed 1. Writable for the tests.
    property real motionFactor: Kirigami.Units.longDuration / 200
    // "no animations" leaves Kirigami 1 ms: that is off
    readonly property bool animationsOff: motionFactor * 200 <= 1
    readonly property int durationFaster: animationsOff ? 0 : Math.round(83 * motionFactor)
    readonly property int durationFast: animationsOff ? 0 : Math.round(167 * motionFactor)
    readonly property int durationNormal: animationsOff ? 0 : Math.round(250 * motionFactor)
    readonly property int durationSlow: animationsOff ? 0 : Math.round(333 * motionFactor)
    readonly property int durationFlash: animationsOff ? 0 : Math.round(1200 * motionFactor)
    // enter: decelerate, like cubic-bezier(0, 0, 0, 1); exit: accelerate
    readonly property int easingEnter: Easing.OutQuint
    readonly property int easingFast: Easing.OutCubic
    readonly property int easingExit: Easing.InCubic

    // --- helpers ------------------------------------------------------

    // Night or Day and the accent, from a window background, its text
    // and the highlight. Before the platform theme is loaded every role
    // is black; Qt's own palette and Plasma's built-in one are what a
    // program gets with no colour scheme at all. Raye never ships those,
    // so all three paint Raye Night with the Raye accent.
    function readScheme(background, text, highlight) {
        if (contrast(background, text) <= 1.5 || isFallbackPalette(background, highlight))
            return { night: true, accent: Qt.color("#7b3cff") }
        return { night: luminance(background) < 0.5, accent: Qt.color(highlight) }
    }

    function isFallbackPalette(background, highlight) {
        const windows = ["#efefef", "#eff0f1"]
        const highlights = ["#308cc6", "#3daee9"]
        return windows.some(c => Qt.colorEqual(background, c))
            && highlights.some(c => Qt.colorEqual(highlight, c))
    }

    // WCAG 2 relative luminance
    function luminance(c) {
        c = Qt.color(c)
        function lin(v) { return v <= 0.04045 ? v / 12.92 : Math.pow((v + 0.055) / 1.055, 2.4) }
        return 0.2126 * lin(c.r) + 0.7152 * lin(c.g) + 0.0722 * lin(c.b)
    }

    function contrast(a, b) {
        const la = luminance(a), lb = luminance(b)
        return (Math.max(la, lb) + 0.05) / (Math.min(la, lb) + 0.05)
    }

    // a moved toward b by t (0..1), opaque, kept on 8 bits
    function mix(a, b, t) {
        a = Qt.color(a); b = Qt.color(b)
        function ch(x, y) { return Math.round((x + (y - x) * t) * 255) / 255 }
        return Qt.rgba(ch(a.r, b.r), ch(a.g, b.g), ch(a.b, b.b), 1)
    }

    // The shades of any accent: hover and pressed a step lighter or
    // darker, text on it white or Raye ink (whichever reads), and an
    // accent text colour lightened (Night) or darkened (Day) until it
    // reads on the window. The Qt style derives them the same way.
    function deriveAccent(accent, isNight) {
        accent = Qt.color(accent)
        const base = Qt.color(isNight ? "#141318" : "#f5f4f7")
        const light = Qt.color("#ffffff"), dark = Qt.color("#17151c")
        const onAccent = contrast(light, accent) >= 4.5 ? light
                       : contrast(dark, accent) >= 4.5 ? dark
                       : contrast(light, accent) >= contrast(dark, accent) ? light : dark
        const lighter = Qt.colorEqual(onAccent, dark)
        const toward = isNight ? "#ffffff" : "#000000"
        let text = accent
        let t = 0
        while (contrast(text, base) < 4.5 && t < 1) {
            t = Math.min(1, t + 0.02)
            text = mix(accent, toward, t)
        }
        const disabled = mix(base, accent, 0.35)
        return {
            hover: mix(accent, isNight || lighter ? "#ffffff" : "#000000", 0.12),
            pressed: mix(accent, lighter ? "#ffffff" : "#000000", isNight ? 0.12 : 0.24),
            text: text,
            disabled: disabled,
            onAccent: onAccent
        }
    }

    // A status name ("success", "caution", "critical", "info") as a colour
    function statusColor(status) {
        switch (status) {
        case "success": return success
        case "caution": case "warning": return caution
        case "critical": case "error": return critical
        case "info": case "informational": return info
        }
        return textSecondary
    }
}
