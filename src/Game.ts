import {Ev} from "./misc/Ev";
import { keyName, keyNames } from "./misc/binds/keyNames";
import { Dir } from "./misc/fs/Dir";
import { DynamicFile } from "./misc/fs/DynamicFile";
import { FsNode } from "./misc/fs/FsNode";

export class Game {
    private commandBuffer: string;
    readonly key: Ev<{key: keyName, down: boolean}>;
    /** `key => isDown` */
    readonly keyStates: Map<keyName, boolean>;
    readonly keyUp: Ev<keyName>;
    readonly keyDown: Ev<keyName>;
    constructor (fs: Dir) {
        this.commandBuffer = "";
        this.key = new Ev();
        this.keyStates = new Map([...keyNames].map((n) => <[keyName, boolean]> [n, false]));
        this.key.addListener(({key, down}) => {
            console.log("%s %s", key, (down) ? "down" : "up");
            this.keyStates.set(key, down);
        });
        this.keyUp = this.key
            .filter<{key: keyName, down: false}>((down) => !down)
            .map<keyName>(({key}) => key);
        this.keyDown = this.key
            .filter<{key: keyName, down: true}>((down) => !!down)
            .map<keyName>(({key}) => key);
        const game = this;
        const binds = new class extends Dir {
            getChild <TType extends FsNode> (name: string) {
                if (
                    (name[0] === "+" ||
                    name[0] === "-") &&
                    name.endsWith(".cfg")
                ) {
                    game.key.dispatch({key: <keyName> name.slice(1, -4), down: name[0] === "+"});
                    const commands = game.commandBuffer;
                    return <TType> <any> new DynamicFile(() => commands);
                }
                return null;
            }
        };
        fs.putChild("binds", binds);
    }
    exec (commands: string) {
        this.commandBuffer += commands + "\n";
    }
}
