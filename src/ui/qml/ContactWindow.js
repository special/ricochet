.pragma library

var windows = { }
var createWindow = function() { console.log("BUG!") }

function getWindow(user) {
    var id = user.address
    var window = windows[user.address]

    if (window === undefined || window === null) {
        window = createWindow(user)
        window.closed.connect(function() { windows[id] = undefined })
        windows[id] = window
    }
    return window
}

function windowExists(user) {
    return windows[user.address] !== undefined && windows[user.address] !== null
}
