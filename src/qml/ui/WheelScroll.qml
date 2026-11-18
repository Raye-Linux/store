// SPDX-License-Identifier: GPL-3.0-or-later
// The mouse wheel, as on Windows: one notch moves the view by the
// "lines per notch" setting (3 by default), about 100 px, in a short
// smooth step. Qt Quick's own Flickable turns a notch into a flick
// velocity instead, which moves a short and uneven distance: that is
// what made the Store feel slow to scroll.
//
//   Flickable { id: flick; WheelScroll { target: flick } }
import QtQuick
import org.kde.kirigami as Kirigami

Kirigami.WheelHandler {
    // ~33 px per line: Windows' 3 lines per notch come to about 100 px
    readonly property real linePixels: 34
    verticalStepSize: Qt.styleHints.wheelScrollLines * linePixels
    horizontalStepSize: Qt.styleHints.wheelScrollLines * linePixels
    keyNavigationEnabled: true
}
