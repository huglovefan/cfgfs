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
	getChild <TType extends FsNode> (name: string) {
		const child = this.children.get(name);
		if (child === void 0) {
			return null;
		}
		return <TType> child;
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
				const next = this.getChild(components[0]);
				if (
					next === null ||
					!(next instanceof Dir)
				) {
					return null;
				}
				return next.lookupPath(components.slice(1));
		}
	}
}
