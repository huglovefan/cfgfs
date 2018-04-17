import {BindSystem} from "./misc/binds/BindSystem";

export const applyBinds = (bindsys: BindSystem) => {
	const keys: {
		[key: string]: {key: string, opposite: string, down: boolean},
	} = {
		forward: {key: "w", opposite: "back", down: false},
		back: {key: "s", opposite: "forward", down: false},
		moveleft: {key: "a", opposite: "moveright", down: false},
		moveright: {key: "d", opposite: "moveleft", down: false},
	};
	for (const [command, dir] of Object.entries(keys)) {
		const oppositeCommand = dir.opposite;
		const opposite = keys[oppositeCommand];
		bindsys.bind(dir.key,
			(c) => {
				dir.down = true;
				if (opposite.down) {
					c(`-${oppositeCommand}`);
				}
				c(`+${command}`);
			},
			(c) => {
				dir.down = false;
				c(`-${command}`);
				if (opposite.down) {
					c(`+${oppositeCommand}`);
				}
			});
	}
};