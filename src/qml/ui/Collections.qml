// SPDX-License-Identifier: GPL-3.0-or-later
// Raye's collections: apps picked by hand for one thing to do. Only the
// apps the catalog has are shown, and a collection with fewer than four
// of them is left out.
pragma Singleton

import QtQuick

QtObject {
    readonly property var all: [
        { id: "work", name: i18nc("@title collection", "Work from home"),
          subtitle: i18nc("@info collection", "Calls, chat, documents, mail"),
          ids: ["us.zoom.Zoom", "com.slack.Slack", "com.github.IsmaelMartinez.teams_for_linux", "org.libreoffice.LibreOffice",
                "org.onlyoffice.desktopeditors", "org.mozilla.thunderbird", "im.riot.Riot", "org.signal.Signal",
                "md.obsidian.Obsidian", "com.nextcloud.desktopclient.nextcloud", "com.bitwarden.desktop", "org.jitsi.jitsi-meet"] },
        { id: "stream", name: i18nc("@title collection", "Start streaming"),
          subtitle: i18nc("@info collection", "Go live, record, clean up your sound"),
          ids: ["com.obsproject.Studio", "io.github.seadve.Kooha", "com.dec05eba.gpu_screen_recorder", "com.github.wwmm.easyeffects",
                "org.audacityteam.Audacity", "org.kde.kdenlive", "org.shotcut.Shotcut", "com.discordapp.Discord"] },
        { id: "music", name: i18nc("@title collection", "Make music"),
          subtitle: i18nc("@info collection", "Record, compose, mix"),
          ids: ["org.ardour.Ardour", "io.lmms.LMMS", "org.musescore.MuseScore", "org.audacityteam.Audacity", "org.tenacityaudio.Tenacity",
                "org.zrythm.Zrythm", "com.bitwig.BitwigStudio", "org.mixxx.Mixxx", "org.hydrogenmusic.Hydrogen", "org.rncbc.qtractor"] },
        { id: "edit", name: i18nc("@title collection", "Edit photos and videos"),
          subtitle: i18nc("@info collection", "Retouch, draw, cut, export"),
          ids: ["org.gimp.GIMP", "org.kde.krita", "org.darktable.Darktable", "org.inkscape.Inkscape", "org.kde.kdenlive",
                "org.shotcut.Shotcut", "org.openshot.OpenShot", "com.rawtherapee.RawTherapee", "org.blender.Blender",
                "fr.handbrake.ghb", "com.github.PintaProject.Pinta", "org.pitivi.Pitivi"] },
        { id: "together", name: i18nc("@title collection", "Play together"),
          subtitle: i18nc("@info collection", "Games and the apps to play with friends"),
          ids: ["com.valvesoftware.Steam", "com.discordapp.Discord", "com.heroicgameslauncher.hgl", "net.lutris.Lutris",
                "net.supertuxkart.SuperTuxKart", "org.luanti.luanti", "org.xonotic.Xonotic", "com.play0ad.zeroad", "org.wesnoth.Wesnoth",
                "com.teeworlds.Teeworlds", "net.veloren.airshipper", "com.moonlight_stream.Moonlight", "org.prismlauncher.PrismLauncher"] }
    ]

    readonly property var racing: ["net.supertuxkart.SuperTuxKart", "com.agateau.PixelWheels", "com.github.juzzlin.DustRacing2D",
        "io.sourceforge.trigger_rally.TriggerRally", "net.sourceforge.ExtremeTuxRacer", "org.tuxfamily.StuntRally",
        "org.kartkrew.RingRacers", "io.jor.cromagrally", "org.srb2.SRB2Kart", "com.q3rally.Q3Rally", "net.sourceforge.torcs",
        "org.gnome.HexGL"]
    readonly property var multiplayer: ["net.supertuxkart.SuperTuxKart", "org.luanti.luanti", "org.xonotic.Xonotic",
        "com.teeworlds.Teeworlds", "com.etlegacy.ETLegacy", "net.froemling.bombsquad", "com.play0ad.zeroad", "org.wesnoth.Wesnoth",
        "org.hedgewars.Hedgewars", "net.veloren.airshipper", "de.blobbyvolley.BlobbyVolley2", "com.fightcade.Fightcade",
        "com.spacestation14.Launcher", "io.github.ezQuake"]

    // the other ways to play what people own on Windows, after Steam and
    // Heroic: Battle.net, EA and Ubisoft through Lutris or Bottles,
    // Minecraft, Roblox, Xbox cloud games, streaming from another PC
    readonly property var launchers: ["net.lutris.Lutris", "com.usebottles.bottles", "com.mojang.Minecraft",
        "org.prismlauncher.PrismLauncher", "org.vinegarhq.Sober", "io.github.unknownskl.greenlight", "io.itch.itch",
        "com.vysp3r.ProtonPlus", "com.moonlight_stream.Moonlight", "com.valvesoftware.SteamLink",
        "com.parsecgaming.parsec", "io.github.benjamimgois.goverlay", "org.libretro.RetroArch"]

    // the apps people look for first, all native: games through Steam and
    // Heroic (Epic, GOG), the usual everyday apps
    readonly property var essentials: [
        "com.valvesoftware.Steam", "com.discordapp.Discord", "com.google.Chrome", "com.spotify.Client",
        "org.mozilla.firefox", "com.obsproject.Studio", "org.videolan.VLC", "org.libreoffice.LibreOffice",
        "com.heroicgameslauncher.hgl", "org.gimp.GIMP", "org.kde.krita", "ai.storyteller.artcraft-launcher"
    ]
}
