import {FsNode} from "./FsNode";

export class DynamicFile extends FsNode {
	private content: string | null;
	private readonly contentGetter: (this: this) => string;
	constructor (contentGetter: DynamicFile["contentGetter"]) {
		super();
		this.content = null;
		this.contentGetter = contentGetter;
	}
	updateContent () {
		this.content = this.contentGetter();
	}
	getContent () {
		if (this.content === null) {
			console.error("File#getContent: this.content === null");
			this.updateContent();
		}
		return this.content!;
	}
}
