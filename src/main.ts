import * as fuse from "fuse-bindings";
import * as mkdirp from "mkdirp";
import * as util from "util";

const mkdirpAsync = util.promisify(mkdirp);

import {Dir} from "./misc/fs/Dir"
import {File} from "./misc/fs/File"
import {splitPath} from "./misc/splitPath";
import {BindSystem} from "./misc/binds/BindSystem";
import {applyBinds} from "./binds";

const PROD = true;

const mountpoint =
	PROD
	? "/home/human/.steam/steam/steamapps/common/Team Fortress 2/tf/custom/cfgfs"
	: "/home/human/test";

const bindsys = new BindSystem();
applyBinds(bindsys);

const root = new Dir(Object.entries({
	cfg: new Dir(Object.entries({
		binds: bindsys.dir,
		"autoexec.cfg": new File(() => {
			// todo: remove once read and exec the real one
			return (
				`echo "hi from cfgfs autoexec"\n` +
				`${bindsys.getInitCommands()}\n` +
				""
			);
		}),
	})),
	r: (() => {
		let n = 0;
		return new File(() => `i've been accessed ${n++} times\n`);
	})(),
}));

(async () => {

await mkdirpAsync(mountpoint);

fuse.mount(mountpoint, {
	options: ["direct_io"],
	readdir: (path, cb) => {
		console.log("readdir %s", path);
		const node = root.lookupPath(splitPath(path));
		if (node === null) {
			cb(0);
			return;
		}
		if (node instanceof Dir) {
			cb(0, node.getContents());
			return;
		}
		if (node instanceof File) {
			cb(0);
			return;
		}
		cb(0);
	},
	getattr: function (path, cb) {
		console.log("getattr %s", path);
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
				nlink: 1,
				size: node.getContents().length,
				mode: 16877,
				uid: process.getuid(),
				gid: process.getgid(),
			});
			return;
		}
		if (node instanceof File) {
			console.log("size of %s before get: %s", path, node.getContent().length);
			node.updateContent();
			const size = node.getContent().length;
			console.log("size of %s after get: %s", path, size);
			cb(0, {
				mtime: new Date(),
				atime: new Date(),
				ctime: new Date(),
				nlink: 1,
				size,
				mode: 33188,
				uid: process.getuid(),
				gid: process.getgid(),
			})
			return;
		}
		cb(fuse.ENOENT);
	},
	open: (path, _flags, cb) => {
		console.log("open %s", path);
		cb(0, 42);
	},
	read: (path, _fd, buf, len, pos, cb) => {
		console.log("read %s", path);
		const node = root.lookupPath(splitPath(path));
		if (node === null) {
			cb(0);
			return;
		}
		if (node instanceof Dir) {
			cb(0);
			return;
		}
		if (node instanceof File) {
			const content = node.getContent();
			const slice = content.slice(pos, pos + len);
			buf.write(slice);
			cb(slice.length);
			return;
		}
		cb(0);
	},
}, (err) => {
	if (err) {
		throw err;
	}
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