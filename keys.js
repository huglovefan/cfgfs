var keys = [];

for (var i = "0".charCodeAt(0); i <= "9".charCodeAt(0); i++) {
    keys.push(String.fromCharCode(i));
}
for (var i = "a".charCodeAt(0); i <= "z".charCodeAt(0); i++) {
    keys.push(String.fromCharCode(i));
}

keys.push("space");

module.exports = keys;
