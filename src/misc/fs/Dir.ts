import {FsNode} from "./FsNode";

export class Dir extends FsNode {
	private readonly children: Map<string, FsNode>;
	constructor (
		children: Iterable<[string, FsNode]> = [],
	) {
		super();
		this.children = new Map(children);
	}
	getContents () {
		return [...this.children.keys()];
	}
	getChild (name: string) {
		const child = this.children.get(name);
		if (child === void 0) {
			return null;
		}
		return child;
	}
	putChild (name: string, node: FsNode) {
		this.children.set(name, node);
	}
	rmChild (name: string) {
		this.children.delete(name);
	}
	lookupPath (components: string[]): FsNode | null {
		switch (components.length) {
			case 0:
				return this;
			case 1:
				return this.getChild(components[0]);
			default:
				const child = this.getChild(components[0]);
				if (child === null) {
					return null;
				}
				if (!(child instanceof Dir)) {
					return null;
				}
				return child.lookupPath(components.slice(1));
		}
	}
}