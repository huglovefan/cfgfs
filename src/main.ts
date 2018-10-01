import * as fuse from "fuse-bindings";
import * as mkdirp from "mkdirp";
import * as util from "util";

const mkdirpAsync = util.promisify(mkdirp);

import {Dir} from "./misc/fs/Dir"
import {DynamicFile} from "./misc/fs/DynamicFile"
import {splitPath} from "./misc/splitPath";
import {Game} from "./Game";

const PROD = false;

const mountpoint =
	PROD
	? "/home/human/.steam/steam/steamapps/common/Team Fortress 2/tf/custom/cfgfs"
	: "/home/human/test";

const root = new Dir(Object.entries({
	r: (() => {
		let n = 0;
		return new DynamicFile(() => `i've been accessed ${n++} times\n`);
	})(),
}));

const game = new Game(root);

(async () => {

await mkdirpAsync(mountpoint);

fuse.mount(mountpoint, {
	options: ["direct_io"],
	readdir: (path, cb) => {
		const node = root.lookupPath(splitPath(path));
		if (node === null) {
			cb(fuse.ENOENT);
			return;
		}
		if (node instanceof Dir) {
			cb(0, [
				".",
				"..",
				...node.getContents(),
			]);
			return;
		}
		if (node instanceof DynamicFile) {
			cb(fuse.ENOTDIR);
			return;
		}
		cb(fuse.EPERM);
	},
	open: (path, _flags, cb) => {
		console.log("open(%s)", JSON.stringify(path));
		const m = /^\/cfg\/binds\/([+-])([^]+)\.cfg$/.exec(path);
		if (m !== null) {
			game.key.dispatch({key: m[2], down: m[1] === "+"});
		}
		cb(fuse.ENOENT);
	},
	release: (path, _flags, cb) => {
		console.log("release(%s)", JSON.stringify(path));
		const node = root.lookupPath(splitPath(path));
		if (node !== null) {
			node.emit("release");
		}
		cb(0);
	},
	getattr: function (path, cb) {
		console.log("getattr(%s)", JSON.stringify(path));
		const node = root.lookupPath(splitPath(path));
		if (node === null) {
			cb(fuse.ENOENT);
			return;
		}
		if (node instanceof Dir) {
			cb(0, {
				mtime: new Date(),
				atime: new Date(),
				ctime: new Date(),
				size: node.getContents().length,
				mode: 16877,
				uid: process.getuid(),
				gid: process.getgid(),
			});
			return;
		}
		if (node instanceof DynamicFile) {
			cb(0, {
				mtime: new Date(),
				atime: new Date(),
				ctime: new Date(),
				/*
				size: (() => {
					console.log("getattr: size of %s before get: %s", path, node.getContent().length);
					node.updateContent();
					const size = node.getContent().length;
					console.log("getattr: size of %s after get: %s", path, size);
					return size;
				})(),
				*/
				size: 0,
				mode: 33188,
				uid: process.getuid(),
				gid: process.getgid(),
			});
			return;
		}
		cb(fuse.ENOENT);
	},
	read: (path, _fd, buf, length, position, cb) => {
		console.log("read(%s)", JSON.stringify(path));
		console.log("read: wants bytes %d-%d (%d)", position, position + length, length);
		const node = root.lookupPath(splitPath(path));
		if (node === null) {
			console.log("read: file doesn't exist");
			cb(fuse.ENOENT);
			return;
		}
		if (node instanceof Dir) {
			cb(fuse.EISDIR);
			return;
		}
		if (node instanceof DynamicFile) {
			const slice = node.getContent().slice(position, position + length);
			const written = slice.length;
			console.log("read: wrote bytes %d-%d (%d)", position, position + written, written);
			buf.write(slice);
			cb(written);
			return;
		}
		cb(fuse.EPERM);
	},
}, (err) => {
	if (err) {
		throw err;
	}
	console.log("mounted cfgfs at %s", mountpoint);
});

process.on("SIGINT", () => {
	fuse.unmount(mountpoint, (err) => {
		if (err) {
			throw err;
		}
		process.exit(0);
	});
});
process.on("SIGTERM", () => {
	fuse.unmount(mountpoint, (err) => {
		if (err) {
			throw err;
		}
		process.exit(0);
	});
});

})().catch((error) => {
	console.error(error);
	process.exit(1);
});
