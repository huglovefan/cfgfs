import * as path from "path";

export const splitPath = (s: string) => {
	return s.split(path.sep).filter((c) => c !== "");
};