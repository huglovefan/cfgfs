function Event () {
    this.callbacks = [];
}

Event.prototype.addListener = function (callback, options) {
    this.callbacks.push({
        callback: callback,
        once: !!options && !!options.once,
    });
};

Event.prototype.removeListener = function (callback) {
    for (var i = 0; i < this.callbacks.length; i++) {
        if (this.callbacks[i].callback === callback) {
            this.callbacks.splice(i, 1);
            i--;
        }
    }
};

Event.prototype.dispatch = function () {
    for (var i = 0; i < this.callbacks.length; i++) {
        this.callbacks[i].callback();
        if (this.callbacks[i].once) {
            this.callbacks.splice(i, 1);
            i--;
        }
    }
};

module.exports = Event;
