export const charsMatching = (re: RegExp) => {
	const result = [];
	for (let i = 0; i <= 65535; i++) {
		const c = String.fromCharCode(i);
		if (re.test(c)) {
			result.push(c);
		}
	}
	return result;
};