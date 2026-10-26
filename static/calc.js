"use strict";

(() => {
	const MAX_LENGTH = 200;
	const MAX_DEPTH = 40;
	const MAX_BITS = 6000;
	const CONSTANTS = { pi: Math.PI, e: Math.E, tau: 2 * Math.PI, phi: (1 + Math.sqrt(5)) / 2 };
	const ALIASES = {
		arcsin: "asin", arccos: "acos", arctan: "atan", arcsinh: "asinh", arccosh: "acosh", arctanh: "atanh",
		ncr: "comb", npr: "perm", fact: "factorial", lg: "log10", sgn: "sign", ceiling: "ceil", cuberoot: "cbrt",
	};
	const TRIG = ["sin", "cos", "tan"];

	let angle = "rad";

	/* a number is exact, a fraction of two BigInts, or inexact, a float; only what cannot be exact is a float */
	const bitLength = (x) => (x < 0n ? -x : x).toString(2).length;

	const bigGcd = (a, b) => {
		a = a < 0n ? -a : a;
		b = b < 0n ? -b : b;
		while (b)
			[a, b] = [b, a % b];
		return a;
	};

	const log2Of = (n) => {
		const bits = bitLength(n);
		return bits <= 1000 ? Math.log2(Number(n)) : Math.log2(Number(n >> BigInt(bits - 1000))) + bits - 1000;
	};

	const isExact = (x) => x.n !== undefined;
	const inexact = (f) => ({ f });

	const toFloat = (x) => {
		if (!isExact(x))
			return x.f;
		let { n, d } = x;
		const shift = BigInt(Math.max(0, Math.max(bitLength(n), bitLength(d)) - 1000));
		n >>= shift;
		d >>= shift;
		return Number(n) / Number(d);
	};

	const rational = (n, d = 1n) => {
		if (d < 0n) {
			n = -n;
			d = -d;
		}
		const g = bigGcd(n, d);
		if (g > 1n) {
			n /= g;
			d /= g;
		}
		return bitLength(n) > MAX_BITS || bitLength(d) > MAX_BITS ? inexact(toFloat({ n, d })) : { n, d };
	};

	const tidy = (x) => Number(x.toPrecision(15));

	const fromFloat = (f) => (Number.isInteger(f) && Math.abs(f) < 2 ** 53 ? rational(BigInt(f)) : inexact(f));

	const decimal = (text) => {
		const m = /^(\d*)(?:\.(\d*))?(?:e([+-]?\d+))?$/.exec(text);
		const fraction = m[2] || "";
		const exponent = m[3] ? Number(m[3]) : 0;
		if (Math.abs(exponent) > 4000)
			throw new SyntaxError("exponent");
		let n = BigInt((m[1] || "") + fraction || "0");
		let d = 10n ** BigInt(fraction.length);
		if (exponent > 0)
			n *= 10n ** BigInt(exponent);
		else
			d *= 10n ** BigInt(-exponent);
		return rational(n, d);
	};

	const isZero = (x) => (isExact(x) ? x.n === 0n : x.f === 0);
	const negate = (x) => (isExact(x) ? { n: -x.n, d: x.d } : inexact(-x.f));
	const sign = (x) => (isExact(x) ? (x.n > 0n ? 1 : x.n < 0n ? -1 : 0) : Math.sign(x.f));

	const floatSum = (a, b) => {
		const r = a + b;
		return fromFloat(Math.abs(r) <= 1e-14 * Math.max(Math.abs(a), Math.abs(b)) ? 0 : tidy(r));
	};

	const add = (a, b) => (isExact(a) && isExact(b) ? rational(a.n * b.d + b.n * a.d, a.d * b.d) :
		floatSum(toFloat(a), toFloat(b)));
	const subtract = (a, b) => add(a, negate(b));
	const multiply = (a, b) => (isExact(a) && isExact(b) ? rational(a.n * b.n, a.d * b.d) :
		fromFloat(tidy(toFloat(a) * toFloat(b))));

	const divide = (a, b) => {
		if (isZero(b))
			throw new RangeError("zero");
		return isExact(a) && isExact(b) ? rational(a.n * b.d, a.d * b.n) : fromFloat(tidy(toFloat(a) / toFloat(b)));
	};

	const floorOf = (n, d) => {
		const q = n / d;
		return n % d !== 0n && n < 0n !== d < 0n ? q - 1n : q;
	};

	const modulo = (a, b) => {
		if (isZero(b))
			throw new RangeError("zero");
		if (isExact(a) && isExact(b)) {
			const q = floorOf(a.n * b.d, a.d * b.n);
			return subtract(a, multiply(b, rational(q)));
		}
		const x = toFloat(a);
		const y = toFloat(b);
		return fromFloat(tidy(((x % y) + y) % y));
	};

	const power = (a, b) => {
		if (isExact(a) && isExact(b) && b.d === 1n && (b.n < 100000n && b.n > -100000n)) {
			const e = b.n < 0n ? -b.n : b.n;
			const size = Math.max(log2Of(a.n < 0n ? -a.n : a.n), log2Of(a.d)) * Number(e);
			if (size + 2 < MAX_BITS) {
				if (b.n >= 0n)
					return rational(a.n ** e, a.d ** e);
				if (a.n === 0n)
					throw new RangeError("zero");
				return rational(a.d ** e, a.n ** e);
			}
		}
		return fromFloat(tidy(Math.pow(toFloat(a), toFloat(b))));
	};

	const compare = (a, b) => sign(subtract(a, b));

	const gamma = (z) => {
		if (z < 0.5)
			return Math.PI / (Math.sin(Math.PI * z) * gamma(1 - z));
		const c = [0.99999999999980993, 676.5203681218851, -1259.1392167224028, 771.32342877765313,
			-176.61502916214059, 12.507343278686905, -0.13857109526572012, 9.9843695780195716e-6,
			1.5056327351493116e-7];
		z -= 1;
		let x = c[0];
		for (let i = 1; i < c.length; i++)
			x += c[i] / (z + i);
		const t = z + 7.5;
		return Math.sqrt(2 * Math.PI) * Math.pow(t, z + 0.5) * Math.exp(-t) * x;
	};

	const factorial = (x) => {
		const f = toFloat(x);
		if (f > 170)
			return inexact(Infinity);
		if (isExact(x) && x.d === 1n) {
			if (x.n < 0n)
				throw new RangeError("factorial");
			let r = 1n;
			for (let i = 2n; i <= x.n; i++)
				r *= i;
			return rational(r);
		}
		return inexact(tidy(gamma(f + 1)));
	};

	const whole = (x) => {
		if (!isExact(x) || x.d !== 1n)
			throw new RangeError("integer");
		return x.n;
	};

	const choose = (nn, kk) => {
		const n = whole(nn);
		let k = whole(kk);
		if (k < 0n || k > n || n > 1000n)
			throw new RangeError("range");
		if (k > n - k)
			k = n - k;
		let r = 1n;
		for (let i = 1n; i <= k; i++)
			r = (r * (n - k + i)) / i;
		return rational(r);
	};

	const permute = (nn, kk) => {
		const n = whole(nn);
		const k = whole(kk);
		if (k < 0n || k > n || n > 1000n)
			throw new RangeError("range");
		let r = 1n;
		for (let i = 0n; i < k; i++)
			r *= n - i;
		return rational(r);
	};

	const greatest = (a, b) => rational(bigGcd(whole(a), whole(b)));
	const least = (a, b) => {
		const x = whole(a);
		const y = whole(b);
		return x === 0n || y === 0n ? rational(0n) : rational(((x < 0n ? -x : x) * (y < 0n ? -y : y)) / bigGcd(x, y));
	};

	const integerSqrt = (n) => {
		if (n < 2n)
			return n;
		let x = 1n << BigInt((bitLength(n) >> 1) + 1);
		for (;;) {
			const y = (x + n / x) >> 1n;
			if (y >= x)
				return x;
			x = y;
		}
	};

	const exactSqrt = (x) => {
		if (x.n < 0n)
			throw new RangeError("sqrt");
		const a = integerSqrt(x.n);
		const b = integerSqrt(x.d);
		return a * a === x.n && b * b === x.d ? rational(a, b) : null;
	};

	const roundTo = (x, digits) => {
		const places = digits ? whole(digits) : 0n;
		if (places < 0n || places > 30n)
			throw new RangeError("places");
		const scale = 10n ** places;
		const scaled = multiply(x, rational(scale));
		const magnitude = scaled.n < 0n ? -scaled.n : scaled.n;
		const nearest = (2n * magnitude + scaled.d) / (2n * scaled.d);
		return rational(scaled.n < 0n ? -nearest : nearest, scale);
	};

	const EXACT = {
		abs: ([x]) => (sign(x) < 0 ? negate(x) : x),
		sign: ([x]) => rational(BigInt(sign(x))),
		floor: ([x]) => rational(floorOf(x.n, x.d)),
		ceil: ([x]) => rational(-floorOf(-x.n, x.d)),
		trunc: ([x]) => rational(x.n / x.d),
		round: ([x, digits]) => roundTo(x, digits),
		min: (xs) => xs.reduce((a, b) => (compare(b, a) < 0 ? b : a)),
		max: (xs) => xs.reduce((a, b) => (compare(b, a) > 0 ? b : a)),
		mod: ([a, b]) => modulo(a, b),
		gcd: ([a, b]) => greatest(a, b),
		lcm: ([a, b]) => least(a, b),
		comb: ([a, b]) => choose(a, b),
		perm: ([a, b]) => permute(a, b),
		factorial: ([x]) => factorial(x),
		sqrt: ([x]) => exactSqrt(x),
		pow: ([a, b]) => power(a, b),
	};

	const integersOnly = () => {
		throw new RangeError("integer");
	};

	const toRadians = (x) => (angle === "deg" ? (x * Math.PI) / 180 : x);
	const fromRadians = (x) => (angle === "deg" ? (x * 180) / Math.PI : x);

	const root = (x, n) => {
		if (n === 0)
			throw new RangeError("root");
		if (x < 0 && Number.isInteger(n) && n % 2 !== 0)
			return -Math.pow(-x, 1 / n);
		return Math.pow(x, 1 / n);
	};

	const logarithm = (x, base) => {
		if (x <= 0)
			throw new RangeError("log");
		return base === undefined ? Math.log10(x) : Math.log(x) / Math.log(base);
	};

	const FUNCTIONS = {
		sin: [1, 1, (x) => Math.sin(toRadians(x))],
		cos: [1, 1, (x) => Math.cos(toRadians(x))],
		tan: [1, 1, (x) => Math.tan(toRadians(x))],
		asin: [1, 1, (x) => fromRadians(Math.asin(x))],
		acos: [1, 1, (x) => fromRadians(Math.acos(x))],
		atan: [1, 1, (x) => fromRadians(Math.atan(x))],
		atan2: [2, 2, (y, x) => fromRadians(Math.atan2(y, x))],
		sinh: [1, 1, Math.sinh],
		cosh: [1, 1, Math.cosh],
		tanh: [1, 1, Math.tanh],
		asinh: [1, 1, Math.asinh],
		acosh: [1, 1, Math.acosh],
		atanh: [1, 1, Math.atanh],
		sqrt: [1, 1, (x) => {
			if (x < 0)
				throw new RangeError("sqrt");
			return Math.sqrt(x);
		}],
		cbrt: [1, 1, Math.cbrt],
		root: [2, 2, root],
		abs: [1, 1, Math.abs],
		sign: [1, 1, Math.sign],
		floor: [1, 1, Math.floor],
		ceil: [1, 1, Math.ceil],
		trunc: [1, 1, Math.trunc],
		round: [1, 2, (x, digits = 0) => Math.round(x * 10 ** digits) / 10 ** digits],
		exp: [1, 1, Math.exp],
		ln: [1, 1, (x) => logarithm(x, Math.E)],
		log: [1, 2, logarithm],
		log10: [1, 1, (x) => logarithm(x)],
		log2: [1, 1, (x) => logarithm(x, 2)],
		pow: [2, 2, Math.pow],
		hypot: [1, 8, Math.hypot],
		min: [1, 8, Math.min],
		max: [1, 8, Math.max],
		mod: [2, 2, (a, b) => ((a % b) + b) % b],
		gcd: [2, 2, integersOnly],
		lcm: [2, 2, integersOnly],
		comb: [2, 2, integersOnly],
		perm: [2, 2, integersOnly],
		factorial: [1, 1, (x) => gamma(x + 1)],
		radians: [1, 1, (x) => (x * Math.PI) / 180],
		degrees: [1, 1, (x) => (x * 180) / Math.PI],
	};

	let lastAnswer = rational(0n);

	const normalize = (text) => {
		let s = text.toLowerCase().trim();
		s = s.replace(/[\u2212\u2013\u2014]/g, "-").replace(/[\u00d7\u00b7\u22c5\u2217]/g, "*").replace(/\u00f7/g, "/");
		s = s.replace(/\u03c0/g, " pi ").replace(/\u221a/g, " sqrt ").replace(/\u221b/g, " cbrt ");
		s = s.replace(/\u00b2/g, "^2").replace(/\u00b3/g, "^3").replace(/\u00b0/g, " deg ");
		s = s.replace(/^(?:what\s+is|what's|whats|calculate|calc|compute|solve)\s+/, "");
		s = s.replace(/[\s=?]+$/, "");
		s = s.replace(/\bto the power of\b|\braised to\b/g, "^").replace(/\b(?:multiplied by|times)\b/g, "*");
		s = s.replace(/\b(?:divided by|over)\b/g, "/").replace(/\bplus\b/g, "+").replace(/\bminus\b/g, "-");
		s = s.replace(/\bmodulo\b/g, "mod").replace(/\bsquared\b/g, "^2").replace(/\bcubed\b/g, "^3");
		return s.replace(/\bper\s?cent\b/g, "%");
	};

	const NUMBER = /^(?:(?:\d{1,3}(?:,\d{3})+(?!\d)|\d+)(?:\.\d*)?|\.\d+)(?:e[+-]?\d+)?/;
	const NUMBER_PLAIN = /^(?:\d+(?:\.\d*)?|\.\d+)(?:e[+-]?\d+)?/;
	const RADIX = /^0(?:x[0-9a-f]+|b[01]+|o[0-7]+)/;

	const tokenize = (s) => {
		const tokens = [];
		let i = 0;
		let depth = 0;
		while (i < s.length) {
			const c = s[i];
			if (c === " " || c === "\t") {
				i++;
				continue;
			}
			const rest = s.slice(i);
			let m = RADIX.exec(rest);
			if (m) {
				tokens.push({ t: "num", v: rational(BigInt(m[0])) });
				i += m[0].length;
				continue;
			}
			m = (depth ? NUMBER_PLAIN : NUMBER).exec(rest);
			if (m) {
				tokens.push({ t: "num", v: decimal(m[0].replace(/,/g, "")) });
				i += m[0].length;
				continue;
			}
			m = /^[a-z_][a-z0-9_]*/.exec(rest);
			if (m) {
				tokens.push({ t: "id", v: Object.hasOwn(ALIASES, m[0]) ? ALIASES[m[0]] : m[0] });
				i += m[0].length;
				continue;
			}
			if (rest.startsWith("**")) {
				tokens.push({ t: "op", v: "^" });
				i += 2;
				continue;
			}
			if ("+-*/^!%,".includes(c)) {
				tokens.push({ t: "op", v: c });
			} else if ("([{".includes(c)) {
				depth++;
				tokens.push({ t: "op", v: "(" });
			} else if (")]}".includes(c)) {
				depth = Math.max(0, depth - 1);
				tokens.push({ t: "op", v: ")" });
			} else {
				throw new SyntaxError("character");
			}
			i++;
		}
		return tokens;
	};

	const ANGLE_WORDS = ["deg", "rad"];
	const WORD_OPERATORS = ["mod", "of"];

	const parse = (tokens) => {
		let p = 0;
		let depth = 0;
		const fail = () => {
			throw new SyntaxError("syntax");
		};
		const isOp = (v, at = p) => tokens[at] && tokens[at].t === "op" && tokens[at].v === v;
		const isWord = (list, at = p) => tokens[at] && tokens[at].t === "id" && list.includes(tokens[at].v);
		const startsTerm = (at) => {
			const t = tokens[at];
			if (!t)
				return false;
			if (t.t === "num")
				return true;
			if (t.t === "op")
				return t.v === "(";
			return !WORD_OPERATORS.includes(t.v) && !ANGLE_WORDS.includes(t.v);
		};

		const parseExpression = () => {
			if (++depth > MAX_DEPTH)
				fail();
			let left = parseMultiplicative();
			while (isOp("+") || isOp("-")) {
				const op = tokens[p++].v;
				left = { t: "bin", op, a: left, b: parseMultiplicative() };
			}
			depth--;
			return left;
		};

		const parseMultiplicative = () => {
			let left = parseUnary();
			for (;;) {
				if (isOp("*") || isOp("/")) {
					left = { t: "bin", op: tokens[p++].v, a: left, b: parseUnary() };
				} else if (isWord(WORD_OPERATORS)) {
					const op = tokens[p++].v === "of" ? "*" : "mod";
					left = { t: "bin", op, a: left, b: parseUnary() };
				} else if (isOp("%") && startsTerm(p + 1)) {
					p++;
					left = { t: "bin", op: "mod", a: left, b: parseUnary() };
				} else if (startsTerm(p)) {
					left = { t: "bin", op: "*", a: left, b: parseUnary() };
				} else {
					return left;
				}
			}
		};

		const parseUnary = () => {
			if (isOp("-")) {
				p++;
				return { t: "neg", a: parseUnary() };
			}
			if (isOp("+")) {
				p++;
				return parseUnary();
			}
			return parsePower();
		};

		const parsePower = () => {
			const base = parsePostfix();
			if (isOp("^")) {
				p++;
				return { t: "bin", op: "^", a: base, b: parseUnary() };
			}
			return base;
		};

		const parsePostfix = () => {
			let node = parsePrimary();
			for (;;) {
				if (isOp("!")) {
					p++;
					node = { t: "fact", a: node };
				} else if (isOp("%") && !startsTerm(p + 1)) {
					p++;
					node = { t: "pct", a: node };
				} else if (isWord(ANGLE_WORDS)) {
					node = { t: tokens[p++].v, a: node };
				} else {
					return node;
				}
			}
		};

		const parseArguments = () => {
			const args = [];
			if (isOp(")")) {
				p++;
				return args;
			}
			for (;;) {
				args.push(parseExpression());
				if (isOp(",")) {
					p++;
					continue;
				}
				if (isOp(")"))
					p++;
				else if (p < tokens.length)
					fail();
				return args;
			}
		};

		const parsePrimary = () => {
			const tok = tokens[p++];
			if (!tok)
				fail();
			if (tok.t === "num")
				return { t: "num", v: tok.v };
			if (tok.t === "op") {
				if (tok.v !== "(")
					fail();
				const inner = parseExpression();
				if (isOp(")"))
					p++;
				else if (p < tokens.length)
					fail();
				return inner;
			}
			if (tok.v === "ans")
				return { t: "num", v: lastAnswer };
			if (Object.hasOwn(CONSTANTS, tok.v))
				return { t: "num", v: inexact(CONSTANTS[tok.v]) };
			if (!Object.hasOwn(FUNCTIONS, tok.v))
				fail();
			let args;
			if (isOp("(")) {
				p++;
				args = parseArguments();
			} else {
				args = [parseUnary()];
			}
			const [least, most] = FUNCTIONS[tok.v];
			if (args.length < least || args.length > most)
				fail();
			return { t: "call", name: tok.v, args };
		};

		const tree = parseExpression();
		if (p < tokens.length)
			fail();
		return tree;
	};

	const callFunction = (name, args) => {
		if (EXACT[name] && args.every(isExact)) {
			const exact = EXACT[name](args);
			if (exact)
				return exact;
		}
		const floats = args.map(toFloat);
		const r = FUNCTIONS[name][2](...floats);
		/* sin(pi) is 1.2e-16 only because pi is not exact */
		return TRIG.includes(name) && Math.abs(r) < 1e-14 && Math.abs(floats[0]) > 1e-6 ? rational(0n) :
			fromFloat(tidy(r));
	};

	const evaluate = (node) => {
		switch (node.t) {
		case "num":
			return node.v;
		case "neg":
			return negate(evaluate(node.a));
		case "pct": {
			const x = evaluate(node.a);
			return isExact(x) ? rational(x.n, x.d * 100n) : fromFloat(tidy(x.f / 100));
		}
		case "fact":
			return factorial(evaluate(node.a));
		case "deg": {
			const x = evaluate(node.a);
			return angle === "deg" ? x : fromFloat(tidy((toFloat(x) * Math.PI) / 180));
		}
		case "rad": {
			const x = evaluate(node.a);
			return angle === "rad" ? x : fromFloat(tidy((toFloat(x) * 180) / Math.PI));
		}
		case "call":
			return callFunction(node.name, node.args.map(evaluate));
		default: {
			const a = evaluate(node.a);
			/* 100 + 10% is 110: a percentage added or taken off is a share of the left side */
			const b = node.b.t === "pct" && (node.op === "+" || node.op === "-") ?
				divide(multiply(a, evaluate(node.b.a)), rational(100n)) : evaluate(node.b);
			switch (node.op) {
			case "+":
				return add(a, b);
			case "-":
				return subtract(a, b);
			case "*":
				return multiply(a, b);
			case "/":
				return divide(a, b);
			case "mod":
				return modulo(a, b);
			default:
				return power(a, b);
			}
		}
		}
	};

	const calculate = (text) => {
		if (!text || text.length > MAX_LENGTH)
			return null;
		try {
			const tree = parse(tokenize(normalize(text)));
			const value = evaluate(tree);
			return isExact(value) || Number.isFinite(value.f) ? { value, tree } : null;
		} catch (e) {
			if (e instanceof SyntaxError || e instanceof RangeError)
				return null;
			throw e;
		}
	};

	const significant = (n, d, sig) => {
		const negative = n < 0n;
		if (negative)
			n = -n;
		const extra = 3;
		let e = n.toString().length - d.toString().length;
		const scaled = (k) => (k >= 0 ? [n * 10n ** BigInt(k), d] : [n, d * 10n ** BigInt(-k)]);
		let k = sig + extra - e;
		let [a, b] = scaled(k);
		let q = a / b;
		while (q.toString().length > sig + extra) {
			k--;
			[a, b] = scaled(k);
			q = a / b;
		}
		while (q.toString().length < sig + extra) {
			k++;
			[a, b] = scaled(k);
			q = a / b;
		}
		e = sig + extra - 1 - k;
		const exact = a % b === 0n && q % 10n ** BigInt(extra) === 0n;
		let text = q.toString();
		let rounded = BigInt(text.slice(0, sig));
		if (text[sig] >= "5")
			rounded++;
		text = rounded.toString();
		if (text.length > sig) {
			text = text.slice(0, sig);
			e++;
		}
		return { digits: text.replace(/0+$/, "") || "0", exponent: e, exact, negative };
	};

	const layout = ({ digits, exponent, negative }, scientific = false) => {
		const sign = negative ? "-" : "";
		if (scientific || exponent >= 21 || exponent < -9) {
			const mantissa = digits.length > 1 ? `${digits[0]}.${digits.slice(1)}` : digits;
			return `${sign}${mantissa}e${exponent < 0 ? "-" : "+"}${Math.abs(exponent)}`;
		}
		if (exponent < 0)
			return `${sign}0.${"0".repeat(-exponent - 1)}${digits}`;
		if (digits.length <= exponent + 1)
			return sign + digits + "0".repeat(exponent + 1 - digits.length);
		return `${sign}${digits.slice(0, exponent + 1)}.${digits.slice(exponent + 1)}`;
	};

	const format = (x) => {
		if (!isExact(x)) {
			if (x.f === 0)
				return "0";
			const size = Math.abs(x.f);
			if (size >= 1e15 || size < 1e-9) {
				const [mantissa, exponent] = x.f.toExponential(11).split("e");
				return `${mantissa.replace(/\.?0+$/, "")}e${exponent}`;
			}
			return x.f.toLocaleString("en-US", { useGrouping: false, maximumSignificantDigits: 12 });
		}
		if (x.n === 0n)
			return "0";
		if (x.d === 1n && x.n.toString().replace("-", "").length <= 21)
			return x.n.toString();
		const long = significant(x.n, x.d, 20);
		return layout(long.exact && x.d !== 1n ? long : significant(x.n, x.d, 12));
	};

	const trivial = (tree) => {
		const inner = tree.t === "neg" ? tree.a : tree;
		return inner.t === "num";
	};

	/*
	 * [symbol, singular, plural, size in the base unit, other names]. A size is exact: a decimal, products
	 * and quotients of decimals, "~" for a float, "@" for a temperature scale, and a leading "/" for a unit
	 * that is an inverse, like litres per 100 km. A name with a capital letter is only matched as typed.
	 */
	const CATEGORIES = [
		{ title: "Length", from: "km", to: "mi", units: [
			["m", "meter", "meters", "1", "metre", "metres"],
			["km", "kilometer", "kilometers", "1000", "kilometre", "kilometres"],
			["cm", "centimeter", "centimeters", "0.01", "centimetre", "centimetres"],
			["mm", "millimeter", "millimeters", "0.001", "millimetre", "millimetres"],
			["µm", "micrometer", "micrometers", "0.000001", "um", "micron", "microns", "micrometre", "micrometres"],
			["nm", "nanometer", "nanometers", "0.000000001", "nanometre", "nanometres"],
			["mi", "mile", "miles", "1609.344", "statute mile", "statute miles"],
			["yd", "yard", "yards", "0.9144", "yds"],
			["ft", "foot", "feet", "0.3048", "fts"],
			["in", "inch", "inches", "0.0254"],
			["nmi", "nautical mile", "nautical miles", "1852"],
			["fur", "furlong", "furlongs", "201.168"],
			["ly", "light year", "light years", "9460730472580800", "lightyear", "lightyears", "light-year", "light-years"],
			["au", "astronomical unit", "astronomical units", "149597870700"],
			["Å", "angstrom", "angstroms", "0.0000000001"],
		] },
		{ title: "Mass", from: "kg", to: "lb", units: [
			["kg", "kilogram", "kilograms", "1", "kilo", "kilos", "kgs"],
			["g", "gram", "grams", "0.001", "gm"],
			["mg", "milligram", "milligrams", "0.000001"],
			["µg", "microgram", "micrograms", "0.000000001", "ug", "mcg"],
			["t", "metric ton", "metric tons", "1000", "tonne", "tonnes", "metric tonne", "metric tonnes"],
			["lb", "pound", "pounds", "0.45359237", "lbs", "lbm"],
			["oz", "ounce", "ounces", "0.028349523125"],
			["st", "stone", "stones", "6.35029318"],
			["ton", "short ton", "short tons", "907.18474", "tons", "us ton", "us tons"],
			["long ton", "long ton", "long tons", "1016.0469088"],
			["gr", "grain", "grains", "0.00006479891"],
			["ct", "carat", "carats", "0.0002"],
		] },
		{ title: "Temperature", from: "°C", to: "°F", units: [
			["°C", "degree Celsius", "degrees Celsius", "@C", "c", "celsius", "centigrade", "degrees c", "degree c", "deg c",
				"degrees celsius", "degree celsius"],
			["°F", "degree Fahrenheit", "degrees Fahrenheit", "@F", "f", "fahrenheit", "degrees f", "degree f", "deg f",
				"degrees fahrenheit", "degree fahrenheit"],
			["K", "kelvin", "kelvins", "@K", "k", "degrees kelvin"],
			["°R", "degree Rankine", "degrees Rankine", "@R", "rankine", "degrees rankine"],
		] },
		{ title: "Time", from: "h", to: "min", units: [
			["s", "second", "seconds", "1", "sec", "secs"],
			["ms", "millisecond", "milliseconds", "0.001", "msec"],
			["µs", "microsecond", "microseconds", "0.000001", "usec"],
			["ns", "nanosecond", "nanoseconds", "0.000000001", "nsec"],
			["min", "minute", "minutes", "60", "mins"],
			["h", "hour", "hours", "3600", "hr", "hrs"],
			["day", "day", "days", "86400"],
			["wk", "week", "weeks", "604800", "wks"],
			["fortnight", "fortnight", "fortnights", "1209600"],
			["mo", "month", "months", "2629746", "mos"],
			["yr", "year", "years", "31556952", "yrs", "y"],
			["decade", "decade", "decades", "315569520"],
			["century", "century", "centuries", "3155695200"],
			["millennium", "millennium", "millennia", "31556952000", "millenium", "millenniums"],
		] },
		{ title: "Area", from: "m²", to: "ft²", units: [
			["m²", "square meter", "square meters", "1", "m2", "m^2", "sq m", "sqm", "square m", "square metre", "square metres", "sq meter", "sq meters", "sq metres"],
			["km²", "square kilometer", "square kilometers", "1000000", "km2", "km^2", "sq km", "sqkm", "square km", "square kilometre", "square kilometres", "sq kilometer", "sq kilometers"],
			["cm²", "square centimeter", "square centimeters", "0.0001", "cm2", "cm^2", "sq cm", "square cm", "square centimetre", "square centimetres", "sq centimeter", "sq centimeters"],
			["ha", "hectare", "hectares", "10000"],
			["ac", "acre", "acres", "4046.8564224"],
			["ft²", "square foot", "square feet", "0.09290304", "ft2", "ft^2", "sq ft", "sqft", "square ft", "sq foot", "sq feet"],
			["in²", "square inch", "square inches", "0.00064516", "in2", "in^2", "sq in", "sqin", "square in", "sq inch", "sq inches"],
			["yd²", "square yard", "square yards", "0.83612736", "yd2", "sq yd", "sqyd", "square yd", "sq yard", "sq yards"],
			["mi²", "square mile", "square miles", "2589988.110336", "mi2", "sq mi", "sqmi", "square mi", "sq mile", "sq miles"],
		] },
		{ title: "Volume", from: "L", to: "gal", units: [
			["L", "liter", "liters", "1", "l", "litre", "litres", "ltr"],
			["mL", "milliliter", "milliliters", "0.001", "ml", "millilitre", "millilitres"],
			["cl", "centiliter", "centiliters", "0.01", "centilitre", "centilitres"],
			["m³", "cubic meter", "cubic meters", "1000", "m3", "m^3", "cubic metre", "cubic metres"],
			["cm³", "cubic centimeter", "cubic centimeters", "0.001", "cm3", "cc", "cubic centimetre", "cubic centimetres"],
			["gal", "gallon", "gallons", "3.785411784", "gals", "us gallon", "us gallons"],
			["qt", "quart", "quarts", "0.946352946", "us quart", "us quarts"],
			["pt", "pint", "pints", "0.473176473", "us pint", "us pints"],
			["cup", "cup", "cups", "0.2365882365", "us cup", "us cups"],
			["fl oz", "fluid ounce", "fluid ounces", "0.0295735295625", "floz", "fl. oz", "us fluid ounce", "us fluid ounces"],
			["tbsp", "tablespoon", "tablespoons", "0.01478676478125", "tbs"],
			["tsp", "teaspoon", "teaspoons", "0.00492892159375"],
			["imp gal", "imperial gallon", "imperial gallons", "4.54609", "uk gallon", "uk gallons"],
			["imp pt", "imperial pint", "imperial pints", "0.56826125", "uk pint", "uk pints"],
			["ft³", "cubic foot", "cubic feet", "28.316846592", "ft3", "cu ft", "cubic ft"],
			["in³", "cubic inch", "cubic inches", "0.016387064", "in3", "cu in", "cubic in"],
			["bbl", "barrel", "barrels", "158.987294928", "oil barrel", "oil barrels"],
		] },
		{ title: "Speed", from: "km/h", to: "mph", units: [
			["m/s", "meter per second", "meters per second", "1", "mps", "m/sec", "metres per second", "metre per second"],
			["km/h", "kilometer per hour", "kilometers per hour", "5/18", "kph", "kmh", "kmph", "km/hr", "kilometres per hour", "kilometre per hour"],
			["mph", "mile per hour", "miles per hour", "0.44704", "mi/h", "mi/hr"],
			["kn", "knot", "knots", "463/900", "kt", "kts"],
			["ft/s", "foot per second", "feet per second", "0.3048", "fps", "ft/sec"],
		] },
		{ title: "Digital storage", from: "GB", to: "MB", units: [
			["b", "bit", "bits", "0.125"],
			["B", "byte", "bytes", "1"],
			["kB", "kilobyte", "kilobytes", "1000", "KB", "kb"],
			["MB", "megabyte", "megabytes", "1000000", "mb"],
			["GB", "gigabyte", "gigabytes", "1000000000", "gb"],
			["TB", "terabyte", "terabytes", "1000000000000", "tb"],
			["PB", "petabyte", "petabytes", "1000000000000000", "pb"],
			["kbit", "kilobit", "kilobits", "125", "Kb"],
			["Mbit", "megabit", "megabits", "125000", "Mb", "mbit"],
			["Gbit", "gigabit", "gigabits", "125000000", "Gb", "gbit"],
			["Tbit", "terabit", "terabits", "125000000000", "Tb", "tbit"],
			["KiB", "kibibyte", "kibibytes", "1024", "kib"],
			["MiB", "mebibyte", "mebibytes", "1048576", "mib"],
			["GiB", "gibibyte", "gibibytes", "1073741824", "gib"],
			["TiB", "tebibyte", "tebibytes", "1099511627776", "tib"],
			["PiB", "pebibyte", "pebibytes", "1125899906842624", "pib"],
		] },
		{ title: "Energy", from: "kcal", to: "kJ", units: [
			["J", "joule", "joules", "1", "j"],
			["kJ", "kilojoule", "kilojoules", "1000", "kj"],
			["cal", "calorie", "calories", "4.184"],
			["kcal", "kilocalorie", "kilocalories", "4184", "Cal", "food calorie", "food calories"],
			["Wh", "watt hour", "watt hours", "3600", "wh", "watt-hour", "watt-hours"],
			["kWh", "kilowatt hour", "kilowatt hours", "3600000", "kwh", "kilowatt-hour", "kilowatt-hours"],
			["BTU", "British thermal unit", "British thermal units", "1055.05585262", "btu", "btus", "british thermal unit", "british thermal units"],
			["eV", "electronvolt", "electronvolts", "1.602176634/10000000000000000000", "ev", "electron volt", "electron volts"],
		] },
		{ title: "Power", from: "hp", to: "kW", units: [
			["W", "watt", "watts", "1", "w"],
			["kW", "kilowatt", "kilowatts", "1000", "kw"],
			["MW", "megawatt", "megawatts", "1000000"],
			["hp", "horsepower", "horsepower", "550*0.45359237*9.80665*0.3048", "mechanical horsepower"],
			["PS", "metric horsepower", "metric horsepower", "735.49875", "ps", "cv"],
			["BTU/h", "BTU per hour", "BTU per hour", "1055.05585262/3600", "btu/h", "btu per hour", "btu/hr"],
		] },
		{ title: "Pressure", from: "bar", to: "psi", units: [
			["Pa", "pascal", "pascals", "1", "pa"],
			["kPa", "kilopascal", "kilopascals", "1000", "kpa"],
			["MPa", "megapascal", "megapascals", "1000000"],
			["bar", "bar", "bars", "100000"],
			["mbar", "millibar", "millibars", "100"],
			["atm", "atmosphere", "atmospheres", "101325", "atmospheres"],
			["psi", "pound per square inch", "pounds per square inch", "0.45359237*9.80665/0.00064516"],
			["mmHg", "millimeter of mercury", "millimeters of mercury", "101325/760", "mmhg", "mm hg"],
			["torr", "torr", "torr", "101325/760"],
		] },
		{ title: "Force", from: "N", to: "lbf", units: [
			["N", "newton", "newtons", "1", "n"],
			["kN", "kilonewton", "kilonewtons", "1000"],
			["lbf", "pound-force", "pounds-force", "0.45359237*9.80665", "pound force", "pounds force"],
			["kgf", "kilogram-force", "kilograms-force", "9.80665", "kilogram force", "kilograms force"],
			["dyn", "dyne", "dynes", "0.00001"],
		] },
		{ title: "Angle", from: "deg", to: "rad", units: [
			["deg", "degree", "degrees", "1", "degs"],
			["rad", "radian", "radians", "~57.29577951308232"],
			["grad", "gradian", "gradians", "0.9", "gon", "gons"],
			["arcmin", "arcminute", "arcminutes", "1/60", "arc minute", "arc minutes"],
			["arcsec", "arcsecond", "arcseconds", "1/3600", "arc second", "arc seconds"],
			["turn", "turn", "turns", "360", "revolution", "revolutions", "rev"],
		] },
		{ title: "Frequency", from: "MHz", to: "GHz", units: [
			["Hz", "hertz", "hertz", "1", "hz"],
			["kHz", "kilohertz", "kilohertz", "1000", "khz"],
			["MHz", "megahertz", "megahertz", "1000000", "mhz"],
			["GHz", "gigahertz", "gigahertz", "1000000000", "ghz"],
			["rpm", "revolution per minute", "revolutions per minute", "1/60"],
		] },
		{ title: "Fuel economy", from: "mpg", to: "L/100 km", units: [
			["km/L", "kilometer per liter", "kilometers per liter", "1", "kpl", "km/l", "kilometres per litre"],
			["L/100 km", "liter per 100 km", "liters per 100 km", "/100", "l/100 km", "l/100km", "L/100km", "litres per 100 km"],
			["mpg", "mile per gallon", "miles per gallon", "1.609344/3.785411784", "mpg us", "us mpg"],
			["mpg UK", "mile per gallon (UK)", "miles per gallon (UK)", "1.609344/4.54609", "mpg uk", "uk mpg", "imperial mpg", "mpg imp"],
		] },
	];

	/* the units to offer for a bare amount: metric for the others and back, each list in the order to try */
	const GROUPS = {
		Length: [["km", "m", "cm", "mm"], ["mi", "yd", "ft", "in"]],
		Mass: [["kg", "g", "mg", "t"], ["lb", "oz", "st"]],
		Area: [["km\u00b2", "ha", "m\u00b2", "cm\u00b2"], ["mi\u00b2", "ac", "ft\u00b2", "in\u00b2"]],
		Volume: [["L", "mL", "m\u00b3", "cl"], ["gal", "qt", "pt", "cup", "fl oz", "tbsp", "tsp"]],
		Temperature: [["\u00b0C", "\u00b0F", "K"]],
		Time: [["h", "min", "s", "day", "wk", "yr"]],
		Speed: [["km/h", "mph", "m/s", "kn"]],
		"Digital storage": [["B", "kB", "MB", "GB", "TB"]],
		Energy: [["kcal", "kJ", "J", "Wh", "kWh"]],
		Power: [["kW", "W", "hp"]],
		Pressure: [["psi", "kPa", "bar", "atm", "Pa"]],
		Force: [["N", "lbf", "kN"]],
		Angle: [["deg", "rad", "grad"]],
		Frequency: [["kHz", "MHz", "GHz", "Hz"]],
		"Fuel economy": [["L/100 km", "mpg", "km/L"]],
	};

	const evaluateSize = (spec) => {
		let size = rational(1n);
		for (const product of spec.split("*")) {
			const [first, ...rest] = product.split("/");
			let term = first ? decimal(first) : rational(1n);
			for (const divisor of rest)
				term = divide(term, decimal(divisor));
			size = multiply(size, term);
		}
		return size;
	};

	const TEMPERATURES = {
		C: [(v) => add(v, decimal("273.15")), (b) => subtract(b, decimal("273.15"))],
		F: [(v) => multiply(add(v, decimal("459.67")), rational(5n, 9n)),
			(b) => subtract(multiply(b, rational(9n, 5n)), decimal("459.67"))],
		K: [(v) => v, (b) => b],
		R: [(v) => multiply(v, rational(5n, 9n)), (b) => multiply(b, rational(9n, 5n))],
	};

	const makeUnit = (category, [symbol, one, many, size, ...names]) => {
		const unit = { category, symbol, one, many, names: [symbol, one, many, ...names], kind: "linear" };
		if (size[0] === "@") {
			unit.kind = "temperature";
			[unit.toBase, unit.fromBase] = TEMPERATURES[size[1]];
		} else if (size[0] === "/") {
			unit.kind = "inverse";
			const k = decimal(size.slice(1));
			unit.toBase = (v) => divide(k, v);
			unit.fromBase = (b) => divide(k, b);
		} else {
			const factor = size[0] === "~" ? inexact(Number(size.slice(1))) : evaluateSize(size);
			unit.toBase = (v) => multiply(v, factor);
			unit.fromBase = (b) => divide(b, factor);
		}
		return unit;
	};

	const EXACT_NAMES = new Map();
	const NAMES = new Map();
	for (const category of CATEGORIES) {
		category.units = category.units.map((spec) => makeUnit(category, spec));
		for (const unit of category.units) {
			for (const name of unit.names) {
				const table = /[A-Z]/.test(name) ? EXACT_NAMES : NAMES;
				const key = table === NAMES ? name.toLowerCase() : name;
				if (!table.has(key))
					table.set(key, []);
				table.get(key).push(unit);
			}
		}
	}

	const findUnits = (phrase) => EXACT_NAMES.get(phrase) || NAMES.get(phrase.toLowerCase()) || [];

	const convertValue = (value, from, to) => to.fromBase(from.toBase(value));

	const quantity = (words) => {
		for (let n = Math.min(3, words.length); n >= 1; n--) {
			const units = findUnits(words.slice(-n).join(" "));
			if (!units.length)
				continue;
			const amount = words.slice(0, -n).join(" ");
			const phrase = words.slice(-n).join(" ");
			if (!amount || /^(?:a|an|one)$/i.test(amount))
				return { value: rational(1n), units, phrase, amount };
			const parsed = calculate(amount);
			if (parsed)
				return { value: parsed.value, units, phrase, amount };
		}
		return null;
	};

	const sharedCategory = (from, to) => {
		for (const a of from) {
			const b = to.find((unit) => unit.category === a.category);
			if (b)
				return { from: a, to: b };
		}
		return null;
	};

	const shortFormat = (x) => {
		if (!isExact(x))
			return format(inexact(Number(x.f.toPrecision(6))));
		if (x.d === 1n || x.n === 0n)
			return format(x);
		const digits = significant(x.n, x.d, 6);
		return layout(digits, digits.exponent >= 6);
	};

	const TEMPERATURE_FORMULAS = {
		"\u00b0C>\u00b0F": "multiply the temperature value by 9/5, then add 32",
		"\u00b0F>\u00b0C": "subtract 32 from the temperature value, then multiply by 5/9",
		"\u00b0C>K": "add 273.15 to the temperature value",
		"K>\u00b0C": "subtract 273.15 from the temperature value",
		"\u00b0F>K": "add 459.67 to the temperature value, then multiply by 5/9",
		"K>\u00b0F": "multiply the temperature value by 9/5, then subtract 459.67",
		"\u00b0C>\u00b0R": "add 273.15 to the temperature value, then multiply by 9/5",
		"\u00b0R>\u00b0C": "multiply the temperature value by 5/9, then subtract 273.15",
		"\u00b0F>\u00b0R": "add 459.67 to the temperature value",
		"\u00b0R>\u00b0F": "subtract 459.67 from the temperature value",
		"K>\u00b0R": "multiply the temperature value by 9/5",
		"\u00b0R>K": "multiply the temperature value by 5/9",
	};

	/* how to get from one unit to another, such as "divide the mass value by 35.274" */
	const formulaText = (from, to) => {
		if (from === to)
			return "";
		if (from.kind === "temperature")
			return TEMPERATURE_FORMULAS[`${from.symbol}>${to.symbol}`] || "";
		const noun = from.category.title.toLowerCase();
		const rate = convertValue(rational(1n), from, to);
		if (from.kind === "inverse" || to.kind === "inverse")
			return `divide ${shortFormat(rate)} by the ${noun} value`;
		return compare(rate, rational(1n)) >= 0 ? `multiply the ${noun} value by ${shortFormat(rate)}` :
			`divide the ${noun} value by ${shortFormat(divide(rational(1n), rate))}`;
	};

	const CONNECTORS = ["to", "in", "into", "as", "equals"];

	/* "5 km to miles", "100f in c", "convert 10 lbs to kg", "how many feet in a mile", "km to miles" */
	const parseConversion = (text) => {
		if (!text || text.length > 120)
			return null;
		let s = text.replace(/[°º]/g, " ").replace(/->|=>|=/g, " to ").replace(/[?!.]+$/, "");
		s = s.replace(/(\d)(?=[a-df-zA-Zµ²³])/g, "$1 ").replace(/\s+/g, " ").trim();
		const many = /^how (?:many|much)\s+/i.exec(s);
		s = s.replace(/^(?:convert|what is|what's|whats|how many|how much)\s+/i, "");
		const words = s.split(" ");
		if (words.length < 3 || words.length > 10)
			return null;
		for (let i = 1; i < words.length - 1; i++) {
			if (!CONNECTORS.includes(words[i].toLowerCase()))
				continue;
			const left = words.slice(0, i);
			const right = words.slice(i + 1).filter((w, j) => j > 0 || !/^(?:a|an|the)$/i.test(w) || words.length - i - 1 === 1);
			if (right.length > 3)
				continue;
			let source;
			let target;
			if (many) {
				target = findUnits(left.join(" "));
				source = quantity(right);
			} else {
				source = quantity(left);
				target = findUnits(right.join(" "));
			}
			const pair = source && target.length && sharedCategory(source.units, target);
			if (pair)
				return { value: source.value, from: pair.from, to: pair.to };
		}
		return null;
	};

	/* "38 oz": a number and a unit alone, shown in the units people usually want next */
	const parseAmount = (text) => {
		if (!text || text.length > 60)
			return null;
		const degrees = /[\u00b0\u00ba]/.test(text);
		const s = text.replace(/[\u00b0\u00ba]/g, " ").replace(/(\d)(?=[a-df-zA-Z\u00b5\u00b2\u00b3])/g, "$1 ").replace(/\s+/g, " ").trim();
		const words = s.split(" ");
		if (words.length > 4 || !/^[-+]?[\d.]/.test(s))
			return null;
		const found = quantity(words);
		if (!found || !found.amount || (found.phrase.length < 2 && !degrees && !/^[glm]$/i.test(found.phrase)))
			return null;
		const from = found.units[0];
		const [first, second] = GROUPS[from.category.title];
		const pool = (!second ? first : first.includes(from.symbol) ? second : second.includes(from.symbol) ? first :
			first.concat(second)).filter((symbol) => symbol !== from.symbol);
		const plain = from.category.title !== "Temperature";
		let lead = null;
		for (const symbol of pool) {
			const to = from.category.units.find((unit) => unit.symbol === symbol);
			let size;
			try {
				size = Math.abs(toFloat(convertValue(found.value, from, to)));
			} catch (e) {
				if (!(e instanceof RangeError))
					throw e;
				continue;
			}
			lead = lead || to;
			if (plain && size >= 1 && size < 10000)
				return { value: found.value, from, to };
		}
		return lead && { value: found.value, from, to: lead };
	};

	const query = document.getElementById("q");
	const results = document.getElementById("results");
	if (!query || !results)
		return;

	const make = (tag, className, text) => {
		const node = document.createElement(tag);
		if (className)
			node.className = className;
		if (text !== undefined)
			node.textContent = text;
		return node;
	};

	/* inserted with the results, in the same paint, so nothing on screen moves */
	const showWithResults = (card) => {
		const arrived = () => results.querySelector(":scope > #answers, :scope > #sidebar, :scope > #urls");
		const observer = new MutationObserver(() => arrived() && reveal());
		const reveal = () => {
			observer.disconnect();
			results.prepend(card);
		};
		observer.observe(results, { childList: true });
		if (arrived())
			reveal();
		document.addEventListener("DOMContentLoaded", () => {
			if (!card.isConnected)
				reveal();
		});
	};

	const showCalculator = (first) => {
		const card = make("section", "calc");
		card.id = "calculator";
		card.dir = "ltr";
		card.setAttribute("aria-label", "Calculator");

		const history = make("div", "calc-history");
		const preview = make("div", "calc-preview");
		preview.setAttribute("aria-live", "polite");
		const field = make("input", "calc-input");
		field.type = "text";
		field.setAttribute("inputmode", "none");
		field.setAttribute("autocomplete", "off");
		field.setAttribute("autocapitalize", "none");
		field.setAttribute("spellcheck", "false");
		field.setAttribute("aria-label", "Calculator expression");

		const head = make("div", "calc-head");
		head.append(history, preview);
		const screen = make("div", "calc-screen");
		screen.append(head, field);

		let finished = false;

		const sizeField = () => {
			const length = field.value.length;
			field.dataset.size = length > 20 ? "xs" : length > 13 ? "s" : "";
		};

		const showPreview = () => {
			const now = finished ? null : calculate(field.value);
			preview.textContent = now && !trivial(now.tree) ? `= ${format(now.value)}` : "";
		};

		const setField = (text, caret) => {
			field.value = text;
			field.setSelectionRange(caret, caret);
			sizeField();
			showPreview();
		};

		const insert = (text) => {
			const start = field.selectionStart;
			const end = field.selectionEnd;
			setField(field.value.slice(0, start) + text + field.value.slice(end), start + text.length);
		};

		const OPERATORS = ["+", "-", "×", "÷", "^", "%", "!", "×10^", "^2"];

		const press = (text) => {
			if (finished) {
				if (OPERATORS.includes(text))
					setField(field.value, field.value.length);
				else
					setField("", 0);
				history.textContent = "";
				finished = false;
			}
			insert(text);
		};

		const equals = () => {
			const now = calculate(field.value);
			if (!now) {
				preview.textContent = "Error";
				return;
			}
			lastAnswer = now.value;
			history.textContent = `${field.value} =`;
			finished = true;
			setField(format(now.value), format(now.value).length);
		};

		const clear = () => {
			finished = false;
			history.textContent = "";
			setField("", 0);
		};

		const backspace = () => {
			if (finished) {
				clear();
				return;
			}
			const start = field.selectionStart;
			const end = field.selectionEnd;
			if (start !== end) {
				insert("");
				return;
			}
			const before = field.value.slice(0, start);
			const cut = /[a-z]+\($/.exec(before);
			const n = cut ? cut[0].length : Math.min(start, 1);
			setField(before.slice(0, start - n) + field.value.slice(start), start - n);
		};

		const KEYS = [];
		let inverse = false;

		const key = (label, aria, action, cls, alt) => {
			const button = make("button", `calc-key ${cls || ""}`, label);
			button.type = "button";
			button.setAttribute("aria-label", aria);
			const spec = { button, label, aria, action, alt };
			button.addEventListener("click", () => {
				(inverse && alt ? alt.action : action)();
				field.focus({ preventScroll: true });
			});
			KEYS.push(spec);
			return button;
		};

		const type = (text) => () => press(text);
		const num = (digit) => key(digit, digit, type(digit), "calc-digit");

		const modeKey = key("Rad", "Switch between radians and degrees", () => {
			angle = angle === "rad" ? "deg" : "rad";
			modeKey.textContent = angle === "rad" ? "Rad" : "Deg";
			modeKey.setAttribute("aria-pressed", String(angle === "deg"));
			showPreview();
		}, "calc-mode");
		modeKey.setAttribute("aria-pressed", "false");

		const invKey = key("Inv", "Inverse functions", () => {
			inverse = !inverse;
			invKey.setAttribute("aria-pressed", String(inverse));
			invKey.classList.toggle("on", inverse);
			for (const spec of KEYS) {
				if (spec.alt) {
					spec.button.textContent = inverse ? spec.alt.label : spec.label;
					spec.button.setAttribute("aria-label", inverse ? spec.alt.aria : spec.aria);
				}
			}
		}, "calc-mode");
		invKey.setAttribute("aria-pressed", "false");

		const fn = (label, aria, text, altLabel, altAria, altText) =>
			key(label, aria, type(text), "calc-fn", { label: altLabel, aria: altAria, action: type(altText) });

		const sci = make("div", "calc-sci");
		sci.id = "calc-sci";
		sci.append(
			modeKey, invKey,
			key("x!", "Factorial", type("!"), "calc-fn"),
			key("(", "Open bracket", type("("), "calc-fn"),
			key(")", "Close bracket", type(")"), "calc-fn"),
			fn("sin", "Sine", "sin(", "sin⁻¹", "Inverse sine", "asin("),
			fn("cos", "Cosine", "cos(", "cos⁻¹", "Inverse cosine", "acos("),
			fn("tan", "Tangent", "tan(", "tan⁻¹", "Inverse tangent", "atan("),
			fn("ln", "Natural logarithm", "ln(", "eˣ", "e to the power", "e^"),
			fn("log", "Logarithm base 10", "log(", "10ˣ", "10 to the power", "10^"),
			fn("√", "Square root", "√(", "x²", "Square", "^2"),
			key("π", "Pi", type("π"), "calc-fn"),
			key("e", "Euler's number", type("e"), "calc-fn"),
			fn("xʸ", "Power", "^", "ʸ√x", "Root", "root("),
			key("Ans", "Previous answer", type("ans"), "calc-fn"),
			key("EXP", "Times ten to the power", type("×10^"), "calc-fn"),
		);

		const pad = make("div", "calc-pad");
		pad.append(
			key("AC", "Clear", clear, "calc-clear"),
			key("⌫", "Delete", backspace, "calc-op"),
			key("%", "Percent", type("%"), "calc-op"),
			key("÷", "Divide", type("÷"), "calc-op"),
			num("7"), num("8"), num("9"),
			key("×", "Multiply", type("×"), "calc-op"),
			num("4"), num("5"), num("6"),
			key("−", "Subtract", type("-"), "calc-op"),
			num("1"), num("2"), num("3"),
			key("+", "Add", type("+"), "calc-op"),
			num("0"), key(".", "Decimal point", type("."), "calc-digit"),
			key("=", "Equals", equals, "calc-equals"),
		);
		pad.lastChild.classList.add("calc-wide");

		const keys = make("div", "calc-keys");
		keys.append(sci, pad);

		const more = make("button", "calc-more", "fx");
		more.type = "button";
		more.setAttribute("aria-label", "Scientific functions");
		more.setAttribute("aria-controls", "calc-sci");
		more.setAttribute("aria-expanded", "false");
		more.addEventListener("click", () => {
			const open = more.getAttribute("aria-expanded") !== "true";
			more.setAttribute("aria-expanded", String(open));
			keys.classList.toggle("sci-open", open);
			field.focus({ preventScroll: true });
		});
		head.prepend(more);

		field.addEventListener("input", () => {
			if (finished) {
				finished = false;
				history.textContent = "";
			}
			sizeField();
			showPreview();
		});
		field.addEventListener("keydown", (event) => {
			if (event.isComposing)
				return;
			if (event.key === "Enter") {
				event.preventDefault();
				equals();
			} else if (finished && event.key === "Backspace") {
				event.preventDefault();
				clear();
			} else if (finished && event.key.length === 1 && !event.ctrlKey && !event.metaKey && !event.altKey) {
				if (!"+-*/^%!".includes(event.key))
					setField("", 0);
				else
					setField(field.value, field.value.length);
				history.textContent = "";
				finished = false;
			}
		});

		card.append(screen, keys);
		showWithResults(card);

		history.textContent = `${query.value.trim()} =`;
		lastAnswer = first.value;
		finished = true;
		setField(format(first.value), format(first.value).length);
	};

	const label = (unit, value) => (isExact(value) && value.n === 1n && value.d === 1n ? unit.one : unit.many);
	const capitalized = (text) => text.charAt(0).toUpperCase() + text.slice(1);

	const showConverter = (found) => {
		let category = found.from.category;
		const card = make("section", "convert");
		card.id = "converter";
		card.dir = "ltr";
		card.setAttribute("aria-label", "Unit converter");

		const picker = make("select", "convert-select convert-category");
		picker.setAttribute("aria-label", "Quantity");
		CATEGORIES.forEach((c, i) => {
			const option = make("option", "", c.title);
			option.value = String(i);
			picker.append(option);
		});

		const row = (name) => {
			const value = make("input", "convert-value");
			value.type = "text";
			value.setAttribute("inputmode", "decimal");
			value.setAttribute("autocomplete", "off");
			value.setAttribute("spellcheck", "false");
			value.setAttribute("aria-label", `${name} value`);
			const unit = make("select", "convert-select convert-unit");
			unit.setAttribute("aria-label", `${name} unit`);
			const line = make("div", "convert-row");
			line.append(value, unit);
			return { value, unit, line };
		};
		const top = row("From");
		const bottom = row("To");
		const swap = make("button", "convert-swap", "⇅");
		swap.type = "button";
		swap.setAttribute("aria-label", "Swap units");
		const formula = make("div", "convert-formula");
		formula.setAttribute("aria-live", "polite");

		const fill = (select, units, chosen) => {
			select.replaceChildren();
			units.forEach((unit, i) => {
				const option = make("option", "", `${capitalized(unit.many)} (${unit.symbol})`);
				option.value = String(i);
				select.append(option);
			});
			select.value = String(units.indexOf(chosen));
		};

		const units = () => [category.units[Number(top.unit.value)], category.units[Number(bottom.unit.value)]];

		const show = (from, to) => {
			const how = formulaText(from, to);
			formula.textContent = capitalized(how);
		};

		const run = (source, target, sourceUnit, targetUnit) => {
			const parsed = calculate(source.value);
			source.setAttribute("aria-invalid", String(!parsed));
			let out = null;
			if (parsed) {
				try {
					out = convertValue(parsed.value, sourceUnit, targetUnit);
				} catch (e) {
					if (!(e instanceof RangeError))
						throw e;
				}
			}
			if (!out || (!isExact(out) && !Number.isFinite(out.f))) {
				target.value = "";
				formula.textContent = "";
				return;
			}
			target.value = shortFormat(out);
			show(sourceUnit, targetUnit);
		};

		const fromBottom = () => {
			const [from, to] = units();
			run(bottom.value, top.value, to, from);
		};
		const fromTopInput = () => {
			const [from, to] = units();
			run(top.value, bottom.value, from, to);
		};

		const choose = (index, from, to) => {
			category = CATEGORIES[index];
			picker.value = String(index);
			fill(top.unit, category.units, from);
			fill(bottom.unit, category.units, to);
		};

		picker.addEventListener("change", () => {
			const next = CATEGORIES[Number(picker.value)];
			const named = (symbol) => next.units.find((unit) => unit.symbol === symbol);
			choose(Number(picker.value), named(next.from), named(next.to));
			fromTopInput();
		});
		top.value.addEventListener("input", fromTopInput);
		bottom.value.addEventListener("input", fromBottom);
		top.unit.addEventListener("change", fromTopInput);
		bottom.unit.addEventListener("change", fromTopInput);
		swap.addEventListener("click", () => {
			const [from, to] = units();
			fill(top.unit, category.units, to);
			fill(bottom.unit, category.units, from);
			fromTopInput();
		});

		choose(CATEGORIES.indexOf(category), found.from, found.to);
		top.value.value = format(found.value);
		fromTopInput();

		const middle = make("div", "convert-middle");
		middle.append(swap);
		card.append(make("div", "convert-head"), top.line, middle, bottom.line, formula);
		card.firstChild.append(picker);
		showWithResults(card);
	};

	const first = calculate(query.value);
	if (first && !trivial(first.tree)) {
		showCalculator(first);
	} else {
		const found = parseConversion(query.value) || parseAmount(query.value);
		if (found)
			showConverter(found);
	}
})();
