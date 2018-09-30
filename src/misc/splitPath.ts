import * as path from "path";

/**
 * splits a path into an array of components
 */
export const splitPath = (s: string) => {
	return s.split(path.sep).filter((c) => c !== "");
};
