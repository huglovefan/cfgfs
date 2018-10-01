var Event = require("./Event.js");
var keys = require("./keys.js");

var game = {};

game.autoexecContent = null;
game.autoexecDone = false;

game.commands = "";
game.exec = function (commands) {
    game.commands += commands + "\n";
};

game.keys = {};
for (var i = 0; i < keys.length; i++) {
    var keyName = keys[i];
    var key = {
        down: false,
        commands: "",
        onpress: new Event(),
        onrelease: new Event(),
    };
    game.keys[keyName] = key;
}

game.bind = function (key, command, options) {
    if (typeof command === "function") {
        if (key[0] === "+") {
            game.keys[key.slice(1)].onpress.addListener(command, options);
        } else if (key[0] === "-") {
            game.keys[key.slice(1)].onrelease.addListener(command, options);
        } else {
            game.keys[key].onrelease.addListener(command, options);
        }
    } else if (typeof command === "string") {
        if (key[0] === "+") {
            game.keys[key.slice(1)].onpress.addListener(function () {
                game.exec(command);
            }, options);
        } else if (key[0] === "-") {
            game.keys[key.slice(1)].onrelease.addListener(function () {
                game.exec(command);
            }, options);
        } else {
            if (command[0] === "+") {
                game.keys[key].onpress.addListener(function () {
                    game.exec(command);
                }, options);
                game.keys[key].onrelease.addListener(function () {
                    game.exec("-" + command.slice(1));
                }, options);
            } else {
                game.keys[key].onpress.addListener(function () {
                    game.exec(command);
                }, options);
            }
        }
    }
};

game.press = function (key) {
    if (key[0] === "+") {
        game.keys[key.slice(1)].down = true;
        game.keys[key.slice(1)].onpress.dispatch();
    } else if (key[0] === "-") {
        game.keys[key.slice(1)].down = false;
        game.keys[key.slice(1)].onrelease.dispatch();
    } else {
        game.press("+" + key);
        game.press("-" + key);
    }
};

module.exports = game;
