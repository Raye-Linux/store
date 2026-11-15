// SPDX-License-Identifier: GPL-3.0-or-later
// The Store's rows, picked from the catalog's feeds.
.pragma library

// The apps of `ids` the catalog lists, in that order, kept by `keep`
// (a function of the app, or null), at most `limit` (0: all).
function pick(catalog, ids, keep, limit) {
    const out = []
    const seen = {}
    for (const id of ids) {
        if (limit > 0 && out.length >= limit)
            break
        const app = catalog.find(id)
        if (app.appId === undefined || !app.listed || seen[app.appId])
            continue
        if (keep && !keep(app))
            continue
        seen[app.appId] = true
        out.push(app)
    }
    return out
}

function kind(k) {
    return app => app.kind === k
}

// the hero wants a picture
function hasScreenshot(app) {
    return app.screenshots && app.screenshots.length > 0
}

// every id once, first lists first
function merge(lists) {
    const out = []
    const seen = {}
    for (const list of lists) {
        for (const id of list) {
            if (!seen[id]) {
                seen[id] = true
                out.push(id)
            }
        }
    }
    return out
}
