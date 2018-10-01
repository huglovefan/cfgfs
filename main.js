var game = require("./game.js");
var fuse = require("fuse-bindings");

var mountpoint = process.argv[2];
if (mountpoint === undefined) {
    console.error("no mount point given");
    process.exit(1);
}

function pathToKey (path) {
    var m = /^\/cfg\/binds\/([+-])([^]+)\.cfg$/.exec(path);
    if (m) {
        var keyName = m[2];
        if (keyName in game.keys) {
            var result = {};
            result.down = (m[1] === "+");
            result.name = m[2];
            return result;
        }
    }
    return null;
}

// TODO: simplify things now that i've gotten it working
fuse.mount(mountpoint, {
    // TODO: was this needed?
	options: ["direct_io"],
    open: function (path, flags, cb) {
        // console.log("open(%o)", path);
        // avoid doing stuff here, tf2 likes opening files without reading them
        if (
            (!game.autoexecDone && path === "/cfg/autoexec.cfg") ||
            !!pathToKey(path)
        ) {
            cb(0);
            return;
        }
        cb(fuse.ENOENT);
    },
    read: function (path, fd, buffer, length, position, cb) {
        // console.log("read(%o)", path);
        // console.log("read: wants bytes %d-%d (%d)", position, position + length, length);
        var content = null;
        if (!game.autoexecDone && path === "/cfg/autoexec.cfg") {
            // TODO: use game.exec for this?
            // for that, should make sure it only does "exec autoexec.cfg" at the start of the game
            console.log("read(%o)", path);
            console.log("read: wants bytes %d-%d (%d)", position, position + length, length);
            if (game.autoexecContent === null) {
                game.autoexecContent = "";
                /*
                alias cfgfs_exec_autoexec_once "exec cfgfs_autoexec"
                cfgfs_autoexec_guard
                alias cfgfs_autoexec_guard "alias cfgfs_exec_autoexec_once noop"
                cfgfs_exec_autoexec_once
                */
                game.autoexecContent += "echo cfgfs: autoexec start\n";
                game.autoexecContent += "exec autoexec.cfg\n";
                for (var key in game.keys) {
                    game.autoexecContent += "alias \"+" + key + "\" \"exec binds/+" + key + ".cfg\"\n";
                    game.autoexecContent += "alias \"-" + key + "\" \"exec binds/-" + key + ".cfg\"\n";
                    game.autoexecContent += "bind \"" + key + "\" \"+" + key + "\"\n";
                }
                game.autoexecContent += "echo cfgfs: autoexec end\n";
                console.log("read: generated autoexec");
            }
            content = game.autoexecContent;
            console.log("read: writing autoexec");
        }
        var keyInfo = pathToKey(path);
        if (keyInfo) {
            if (position === 0) {
                console.log("read(%o)", path);
                console.log("read: wants bytes %d-%d (%d)", position, position + length, length);
                game.keys[keyInfo.name].down = keyInfo.down;
                if (keyInfo.down) {
                    game.keys[keyInfo.name].onpress.dispatch();
                } else {
                    game.keys[keyInfo.name].onrelease.dispatch();
                }
                game.keys[keyInfo.name].commands = game.commands;
                game.commands = "";
                game.keys[keyInfo.name].commands.split("\n").filter(function (line) {
                    return line !== "";
                }).map(function (line) {
                    return "> " + line;
                }).forEach(function (line) {
                    console.log(line);
                });
            }
            content = game.keys[keyInfo.name].commands;
        }
        if (content !== null) {
            var slice = content.slice(position, position + length);
            var written = 0;
            written += buffer.write(slice);
            if (written !== 0 && position + slice.length === content.length) {
                // sent everything
                written += buffer.write("\0", written);
                game.keys[keyInfo.name].commands = "";
                console.log("added null byte");
            }
            cb(written);
            console.log("read: wrote bytes %d-%d (%d)", position, position + slice.length, slice.length);
            return;
        }
        cb(fuse.ENOENT);
    },
    release: function (path, flags, cb) {
        // console.log("release(%o)", path);
        if (!game.autoexecDone && path === "/cfg/autoexec.cfg") {
            if (game.autoexecContent === null) {
                console.log("release: autoexec wasn't read, not destroying");
            } else {
                game.autoexecDone = true;
                game.autoexecContent = "";
                console.log("release: destroyed autoexec");
            }
        }
        var keyInfo = pathToKey(path);
        if (keyInfo) {
            game.keys[keyInfo.name].commands = "";
            cb(0);
            return;
        }
        cb(fuse.ENOENT);
    },
    readdir: function (path, cb) {
        // console.log("readdir(%o)", path);
        if (path === "/") {
            cb(0, [
                ".",
                "..",
                "cfg",
            ]);
            return;
        }
        if (path === "/cfg") {
            var names = [
                ".",
                "..",
            ];
            if (!game.autoexecDone) {
                names.push("autoexec.cfg");
            }
            names.push("binds");
            cb(0, names);
            return;
        }
        if (path === "/cfg/binds") {
            var names = [
                ".",
                "..",
            ];
            // want them sorted
            for (var key in game.keys) {
                names.push("+" + key + ".cfg");
            }
            for (var key in game.keys) {
                names.push("-" + key + ".cfg");
            }
            cb(0, names);
            return;
        }
        cb(fuse.ENOENT);
    },
    getattr: function (path, cb) {
        // console.log("getattr(%o)", path);
        // TODO: these don't need to be correct
        var stat = {
            mtime: new Date(),
            atime: new Date(),
            ctime: new Date(),
            uid: process.getuid(),
            gid: process.getgid(),
            mode: null,
        };
        switch (path) {
            case "/":
                stat.mode = 16877;
                stat.size = 1;
                break;
            case "/cfg":
                stat.mode = 16877;
                stat.size = 1;
                if (!game.autoexecDone) {
                    stat.size++;
                }
                break;
            case "/cfg/binds":
                stat.mode = 16877;
                stat.size = 0;
                for (var key in game.keys) {
                    stat.size += 2;
                }
                break;
            default:
                if (
                    (!game.autoexecDone && path === "/cfg/autoexec.cfg") ||
                    !!pathToKey(path)
                ) {
                    if (!game.autoexecDone && path === "/cfg/autoexec.cfg") {
                        console.log("getattr: stat'd autoexec");
                    }
                    stat.mode = 33188;
                    // it reads exactly this many bytes, -1 or 0 won't work
                    // i think the maximum size for a config is 1MB
                    stat.size = 1024 * 1024;
                }
                break;
        }
        if (stat.mode !== null) {
            cb(0, stat);
            return;
        }
        cb(fuse.ENOENT);
    },
}, function (error) {
    if (error) {
        console.error(error);
        process.exit(1);
    }
    require("./binds.js");
});

function cleanup () {
    console.log("cleanup: unmounting - autoexecDone: %o", game.autoexecDone);
    fuse.unmount(mountpoint, function (error) {
        if (error) {
            console.error(error);
        }
        process.exit((error) ? 1 : 0);
    });
}

process.on("SIGINT", cleanup);
process.on("SIGTERM", cleanup);
