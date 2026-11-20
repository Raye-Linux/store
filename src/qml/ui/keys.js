// SPDX-License-Identifier: GPL-3.0-or-later
// The keyboard on a page, as in a Windows app: the arrows, Page Up and
// Page Down, Home and End, and Space scroll the page; in a grid of cards
// the arrows move from card to card.
.pragma library

// Qt's key codes and modifiers (the values of Qt.Key_* and Qt.*Modifier),
// so the file can be read without QML
const KEY_SPACE = 0x20
const KEY_HOME = 0x01000010
const KEY_END = 0x01000011
const KEY_LEFT = 0x01000012
const KEY_UP = 0x01000013
const KEY_RIGHT = 0x01000014
const KEY_DOWN = 0x01000015
const KEY_PAGE_UP = 0x01000016
const KEY_PAGE_DOWN = 0x01000017
const SHIFT = 0x02000000
const CONTROL = 0x04000000
const ALT = 0x08000000
const META = 0x10000000

// one arrow press: about a third of a wheel notch on Windows' 3 lines
const STEP = 48

// Where a page that shows `height` of `contentHeight` px, now at
// `contentY` (from `originY`), goes for this key; null when the key does
// not scroll. Never past either end.
function scrollTo(key, modifiers, contentY, height, contentHeight, originY) {
    const origin = originY || 0
    const bottom = origin + Math.max(0, contentHeight - height)
    const page = Math.max(STEP, height - STEP)
    const shift = (modifiers & SHIFT) !== 0
    // Ctrl, Alt and the Windows key belong to shortcuts
    if ((modifiers & (CONTROL | ALT | META)) !== 0)
        return null
    let to
    switch (key) {
    case KEY_UP: to = contentY - STEP; break
    case KEY_DOWN: to = contentY + STEP; break
    case KEY_PAGE_UP: to = contentY - page; break
    case KEY_PAGE_DOWN: to = contentY + page; break
    case KEY_HOME: to = origin; break
    case KEY_END: to = bottom; break
    case KEY_SPACE: to = shift ? contentY - page : contentY + page; break
    default: return null
    }
    return Math.min(bottom, Math.max(origin, to))
}

// false while the key belongs to what has the focus: a text field takes
// every key, a list or a grid takes its own arrows
function pageTakes(key, focusIsText, focusIsList) {
    if (focusIsText)
        return false
    if (focusIsList && (key === KEY_UP || key === KEY_DOWN || key === KEY_LEFT || key === KEY_RIGHT))
        return false
    return true
}

// The card the arrows move to in a grid of `count` cards, `columns`
// wide; `index` -1 when none has the focus yet (the first one then).
// -1 when the key does not move, the same index at an edge.
function gridMove(key, index, count, columns) {
    if (count <= 0)
        return -1
    const cols = Math.max(1, columns)
    if (index < 0 || index >= count)
        return (key === KEY_LEFT || key === KEY_RIGHT || key === KEY_UP || key === KEY_DOWN) ? 0 : -1
    switch (key) {
    case KEY_LEFT: return Math.max(0, index - 1)
    case KEY_RIGHT: return Math.min(count - 1, index + 1)
    case KEY_UP: return index - cols >= 0 ? index - cols : index
    case KEY_DOWN:
        // the last row may be short: Down from above it lands on its last card
        if (index + cols < count)
            return index + cols
        return Math.floor(index / cols) < Math.floor((count - 1) / cols) ? count - 1 : index
    case KEY_HOME: return 0
    case KEY_END: return count - 1
    }
    return -1
}
