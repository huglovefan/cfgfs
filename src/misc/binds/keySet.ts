import {charsMatching} from "../charsMatching";

export const keySet = new Set([
	...charsMatching(/[a-z]/),
]);