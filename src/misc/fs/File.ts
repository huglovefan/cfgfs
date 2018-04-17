import {FsNode} from "./FsNode";

export class File extends FsNode {
	private content: string;
	private readonly contentGetter: () => string;
	constructor (contentGetter: File["contentGetter"]) {
		super();
		this.content = "";
		this.contentGetter = contentGetter;
	}
	updateContent () {
		const contentGetter = this.contentGetter;
		this.content = contentGetter();
	}
	getContent () {
		return this.content;
	}
}