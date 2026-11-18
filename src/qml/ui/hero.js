// SPDX-License-Identifier: GPL-3.0-or-later
// The big picture on top of Home and Apps: apps most people know, picked
// by Raye. Never a feed's pick: Flathub's picks of the week can be any
// app, a password cracker included. Another one leads each day.
.pragma library

var apps = ["org.mozilla.firefox", "com.discordapp.Discord", "com.valvesoftware.Steam", "com.obsproject.Studio",
            "org.gimp.GIMP", "org.libreoffice.LibreOffice", "org.videolan.VLC", "com.spotify.Client",
            "org.kde.krita", "org.blender.Blender", "org.kde.kdenlive", "org.mozilla.thunderbird",
            "org.audacityteam.Audacity", "org.inkscape.Inkscape"]

// the list, turned so that day `day` (days since 1970) starts it
function forDay(day) {
    var n = apps.length
    var start = ((Math.floor(day) % n) + n) % n
    return apps.slice(start).concat(apps.slice(0, start))
}

// the list for the local date of `now`
function today(now) {
    return forDay(Math.floor((now.getTime() - now.getTimezoneOffset() * 60000) / 86400000))
}
