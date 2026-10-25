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
		gcd: [2, 2, () => {
			throw new RangeError("integer");
		}],
		lcm: [2, 2, () => {
			throw new RangeError("integer");
		}],
		comb: [2, 2, () => {
			throw new RangeError("integer");
		}],
		perm: [2, 2, () => {
			throw new RangeError("integer");
		}],
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

	const layout = ({ digits, exponent, negative }) => {
		const sign = negative ? "-" : "";
		if (exponent >= 21 || exponent < -9) {
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

	const query = document.getElementById("q");
	const results = document.getElementById("results");
	if (!query || !results)
		return;

	const first = calculate(query.value);
	if (!first || trivial(first.tree))
		return;

	const make = (tag, className, text) => {
		const node = document.createElement(tag);
		if (className)
			node.className = className;
		if (text !== undefined)
			node.textContent = text;
		return node;
	};

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

	const keys = make("div", "calc-keys");
	keys.append(
		modeKey,
		key("x!", "Factorial", type("!"), "calc-fn"),
		key("(", "Open bracket", type("("), "calc-fn"),
		key(")", "Close bracket", type(")"), "calc-fn"),
		key("%", "Percent", type("%"), "calc-op"),
		key("⌫", "Delete", backspace, "calc-op"),
		key("AC", "Clear", clear, "calc-clear"),

		invKey,
		fn("sin", "Sine", "sin(", "sin⁻¹", "Inverse sine", "asin("),
		fn("ln", "Natural logarithm", "ln(", "eˣ", "e to the power", "e^"),
		num("7"), num("8"), num("9"),
		key("÷", "Divide", type("÷"), "calc-op"),

		key("π", "Pi", type("π"), "calc-fn"),
		fn("cos", "Cosine", "cos(", "cos⁻¹", "Inverse cosine", "acos("),
		fn("log", "Logarithm base 10", "log(", "10ˣ", "10 to the power", "10^"),
		num("4"), num("5"), num("6"),
		key("×", "Multiply", type("×"), "calc-op"),

		key("e", "Euler's number", type("e"), "calc-fn"),
		fn("tan", "Tangent", "tan(", "tan⁻¹", "Inverse tangent", "atan("),
		fn("√", "Square root", "√(", "x²", "Square", "^2"),
		num("1"), num("2"), num("3"),
		key("−", "Subtract", type("-"), "calc-op"),

		key("Ans", "Previous answer", type("ans"), "calc-fn"),
		key("EXP", "Times ten to the power", type("×10^"), "calc-fn"),
		fn("xʸ", "Power", "^", "ʸ√x", "Root", "root("),
		num("0"), key(".", "Decimal point", type("."), "calc-digit"),
		key("=", "Equals", equals, "calc-equals"),
		key("+", "Add", type("+"), "calc-op"),
	);

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
	/* inserted with the results, in the same paint, so nothing on screen moves */
	const reveal = () => {
		observer.disconnect();
		results.prepend(card);
	};
	const arrived = () => results.querySelector(":scope > #answers, :scope > #sidebar, :scope > #urls");
	const observer = new MutationObserver(() => arrived() && reveal());
	observer.observe(results, { childList: true });
	if (arrived())
		reveal();
	document.addEventListener("DOMContentLoaded", () => {
		if (!card.isConnected)
			reveal();
	});

	history.textContent = `${query.value.trim()} =`;
	lastAnswer = first.value;
	finished = true;
	setField(format(first.value), format(first.value).length);
})();
