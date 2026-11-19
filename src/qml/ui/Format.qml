// SPDX-License-Identifier: GPL-3.0-or-later
// Words and numbers as the Store shows them: category names, "4.5",
// "1.2K", the source of an app.
pragma Singleton

import QtQuick

QtObject {
    // the categories the Store browses by, in the order it shows them;
    // "match" are the AppStream categories each one takes in
    readonly property var appCategories: [
        { id: "Office", name: i18nc("@title category", "Productivity"), match: ["Office"] },
        { id: "Graphics", name: i18nc("@title category", "Photo & design"), match: ["Graphics", "Photography"] },
        { id: "AudioVideo", name: i18nc("@title category", "Music & video"), match: ["AudioVideo", "Audio", "Video"] },
        { id: "Network", name: i18nc("@title category", "Internet"), match: ["Network", "WebBrowser"] },
        { id: "InstantMessaging", name: i18nc("@title category", "Social"), match: ["InstantMessaging", "Chat"] },
        { id: "Education", name: i18nc("@title category", "Education"), match: ["Education", "Science"] },
        { id: "Development", name: i18nc("@title category", "Developer tools"), match: ["Development"] },
        { id: "Utility", name: i18nc("@title category", "Utilities"), match: ["Utility", "System"] }
    ]
    readonly property var gameCategories: [
        { id: "ActionGame", name: i18nc("@title game genre", "Action"), match: ["ActionGame", "Shooter"] },
        { id: "AdventureGame", name: i18nc("@title game genre", "Adventure"), match: ["AdventureGame", "RolePlaying"] },
        { id: "StrategyGame", name: i18nc("@title game genre", "Strategy"), match: ["StrategyGame"] },
        { id: "LogicGame", name: i18nc("@title game genre", "Puzzle"), match: ["LogicGame", "BoardGame", "CardGame"] },
        { id: "Simulation", name: i18nc("@title game genre", "Simulation"), match: ["Simulation", "SportsGame"] },
        { id: "Emulator", name: i18nc("@title game genre", "Emulators"), match: ["Emulator"] },
        { id: "PackageManager", name: i18nc("@title game genre", "Launchers"), match: ["PackageManager"] }
    ]

    // the one word under a card: "Games", "Photo & design"...
    function categoryName(categories) {
        if (!categories)
            return ""
        if (categories.indexOf("Game") >= 0)
            return i18nc("@info category of one app", "Game")
        for (const c of appCategories) {
            if (c.match.some(m => categories.indexOf(m) >= 0))
                return c.name
        }
        return ""
    }

    // the AppStream category an app is browsed under: "Game" for games
    function mainCategory(categories) {
        if (!categories)
            return ""
        if (categories.indexOf("Game") >= 0)
            return "Game"
        for (const c of appCategories) {
            const hit = c.match.find(m => categories.indexOf(m) >= 0)
            if (hit)
                return hit
        }
        return categories.length > 0 ? categories[0] : ""
    }

    function inCategory(categories, category) {
        if (!categories)
            return false
        const all = appCategories.concat(gameCategories)
        const entry = all.find(c => c.id === category)
        const names = entry ? entry.match : [category]
        return names.some(m => categories.indexOf(m) >= 0)
    }

    // 950, 1.2K, 3.4M
    function count(n) {
        if (n < 1000)
            return Number(n).toLocaleString(Qt.locale(), "f", 0)
        if (n < 1000000)
            return i18nc("@info a number in thousands, as in 1.2K ratings", "%1K",
                         Number(n / 1000).toLocaleString(Qt.locale(), "f", n < 10000 ? 1 : 0))
        return i18nc("@info a number in millions, as in 3.4M downloads", "%1M",
                     Number(n / 1000000).toLocaleString(Qt.locale(), "f", 1))
    }

    // 1.1 GB, 7 MB: a download's size, never cut between number and unit
    function size(bytes) {
        return sizeWords(bytes).replace(" ", "\u00a0")
    }
    function sizeWords(bytes) {
        if (bytes >= 1024 * 1024 * 1024)
            return i18nc("a size in gigabytes", "%1 GB", Number(bytes / (1024 * 1024 * 1024)).toLocaleString(Qt.locale(), "f", 1))
        if (bytes >= 1024 * 1024)
            return i18nc("a size in megabytes", "%1 MB", Number(Math.round(bytes / (1024 * 1024))).toLocaleString(Qt.locale(), "f", 0))
        return i18nc("a size in kilobytes", "%1 KB", Number(Math.max(1, Math.round(bytes / 1024))).toLocaleString(Qt.locale(), "f", 0))
    }

    function rating(avg) {
        return Number(avg).toLocaleString(Qt.locale(), "f", 1)
    }

    // under ten ratings a number says nothing yet
    function hasRating(app) {
        return app && app.ratingCount >= 10 && app.ratingAvg > 0
    }

    function source(app) {
        if (!app)
            return ""
        return app.source === "flatpak" ? i18nc("@info where the app comes from", "Flathub")
             : app.source === "raye" ? i18nc("@info where the app comes from", "Raye, from its makers")
             : app.source === "windows" && app.urls && app.urls.download ? i18nc("@info where the app comes from", "Its maker's page")
             : app.source === "windows" ? i18nc("@info where the app comes from", "Its maker's installer, checked by Raye")
             : i18nc("@info where the app comes from", "Raye Linux packages")
    }

    // what Raye says about a Windows program, or its native version
    function windowsLabel(app) {
        switch (app ? app.windowsState : "") {
        case "optimized": return i18nc("@info a Windows program checked one by one", "Optimized for Raye")
        case "works": return i18nc("@info a Windows program on the standard settings", "Works on Raye")
        case "blocked": return i18nc("@info a game whose anti-cheat refuses this system", "Doesn't work on Raye")
        case "native": return i18nc("@info the version made for this system, not the Windows one", "Native app")
        }
        return ""
    }
    function windowsTint(app) {
        switch (app ? app.windowsState : "") {
        case "optimized": return Theme.accentText
        case "native": return Theme.success
        case "blocked": return Theme.caution
        }
        return Theme.textSecondary
    }
    function windowsExplained(app) {
        switch (app ? app.windowsState : "") {
        case "optimized": return i18n("Raye checked this program itself, on a real PC, and runs it with settings made for it.")
        case "works": return i18n("Runs with Raye's standard settings for Windows programs. It hasn't been checked one by one yet.")
        case "blocked": return app.windowsAntiCheat
                               ? i18n("Its anti-cheat refuses this system. Raye won't try to get around it: that gets accounts banned.")
                               : i18n("This one can't run on Raye for now, for the reason given with it.")
        case "native": return i18n("This is the native version, not the Windows one. It starts faster and fits in better on Raye.")
        }
        return ""
    }

    // the Windows programs with no version here that this app stands in for
    function replacesText(app) {
        const names = app && app.replaces ? app.replaces : []
        if (names.length === 0)
            return ""
        return i18nc("@info %1 is one to three Windows program names", "Instead of %1",
                     names.slice(0, 3).join(i18nc("@info separator between program names", ", ")))
    }
    function replacesExplained(app) {
        const names = app && app.replaces ? app.replaces : []
        return names.length === 1 ? i18n("%1 has no version for Raye. This app does the same kind of work.", names[0])
             : names.length > 1 ? i18n("These Windows programs have no version for Raye. This app does the same kind of work.")
             : ""
    }
    // what Raye does for a game launcher, on its page
    function launcherNote(app) {
        switch (app ? app.appId : "") {
        case "com.valvesoftware.Steam":
        case "com.valvesoftware.Steam.desktop":
            return i18n("Raye turns on Steam Play for every game when it installs Steam, so Windows games install and start with one click. Games whose anti-cheat refuses Linux still won't start.")
        case "com.heroicgameslauncher.hgl":
        case "com.heroicgameslauncher.hgl.desktop":
            return i18n("Sign in to Epic Games, GOG and Amazon from Heroic to see all your games. The first time it starts, Heroic downloads what Windows games need to run.")
        }
        return ""
    }

    function date(d) {
        if (!d)
            return ""
        const when = new Date(d)
        return isNaN(when.getTime()) ? ""
             : when.toLocaleDateString(Qt.locale(), i18nc("@info date format, Qt syntax", "MMMM d, yyyy"))
    }

    // a branding colour for a card's tile, or the Raye violet
    function brand(app) {
        if (app && app.brandDark && app.brandDark !== "")
            return app.brandDark
        if (app && app.brandLight && app.brandLight !== "")
            return app.brandLight
        return "#7b3cff"
    }
}
