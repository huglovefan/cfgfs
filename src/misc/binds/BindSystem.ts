import {Dir} from "../fs/Dir";
import {File} from "../fs/File";
import {keySet} from "./keySet";
import {EmptyFile} from "../fs/EmptyFile";

type BindCallback = (c: (c: string) => void) => void | string;

export class BindSystem {
	readonly dir: Dir;
	constructor () {
		this.dir = new Dir();
		for (const key of keySet) {
			this.dir.putChild(`+${key}.cfg`, EmptyFile);
			this.dir.putChild(`-${key}.cfg`, EmptyFile);
		}
	}
	bind (
		key: string,
		onDown?: BindCallback,
		onUp?: BindCallback,
	) {
		for (const [filename, callback] of <
			[string, BindCallback | undefined][]
		> [
			[`+${key}.cfg`, onDown],
			[`-${key}.cfg`, onUp],
		]) {
			this.dir.putChild(filename, (callback !== void 0) ? new File(() => {
				let res = [];
				const cb_res = callback((c) => {
					res.push(c);
				});
				if (cb_res !== void 0) {
					res.push(cb_res);
				}
				return res.join("\n") + "\n";
			}) : EmptyFile);
		}
	}
	unbind (key: string) {
		this.bind(key);
	}
	getInitCommands () {
		const commands = [];
		for (const key of keySet) {
			commands.push(`alias "+cfgfs_${key}" "exec binds/+${key}.cfg"`);
			commands.push(`alias "-cfgfs_${key}" "exec binds/-${key}.cfg"`);
			commands.push(`bind "${key}" "+cfgfs_${key}"`);
		}
		return commands.join("\n");
	}
}