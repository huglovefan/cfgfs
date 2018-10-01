var game = require("./game.js");

//
//
//

function nullmovementify (key, opposite, move, moveopposite) {
    game.bind("+" + key, function () {
        if (game.keys[opposite].down) {
            game.exec("-" + moveopposite);
        }
        game.exec("+" + move);
    });
    game.bind("-" + key, function () {
        game.exec("-" + move);
        if (game.keys[opposite].down) {
            game.exec("+" + moveopposite);
        }
    });
}

function nullmovementifyPair (key, opposite, move, moveopposite) {
    nullmovementify(key, opposite, move, moveopposite);
    nullmovementify(opposite, key, moveopposite, move);
}

nullmovementifyPair("w", "s", "forward", "back");
nullmovementifyPair("a", "d", "moveleft", "moveright");

//
//
//

// try not to stop when double jumping
// doesn't work that well

var lastDirection = null;

var directionKeys = ["w", "s", "a", "d"];
for (var i = 0; i < directionKeys.length; i++) {
    (function (key) {
        game.bind("+" + key, function () {
            lastDirection = key;
        });
    })(directionKeys[i]);
}

game.bind("+space", function () {
    var isDirectionKeyPressed = false;
    for (var i = 0; i < directionKeys.length; i++) {
        if (game.keys[directionKeys[i]].down) {
            isDirectionKeyPressed = true;
            break;
        }
    }
    console.log("isDirectionKeyPressed = %o", isDirectionKeyPressed);
    console.log("lastDirection = %o", lastDirection);
    var lastDirection_ = lastDirection;
    if (!isDirectionKeyPressed && lastDirection_) {
        game.press("+" + lastDirection_);
    }
    game.exec("+jump");
    if (!isDirectionKeyPressed && lastDirection_) {
        game.bind("-space", function () {
            game.press("-" + lastDirection_);
        }, {once: true});
    }
});
game.bind("-space", "-jump");
