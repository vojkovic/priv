"use strict";

(() => {
	const query = document.getElementById("q");
	const results = document.getElementById("results");
	if (!query || !results)
		return;

	const clamp = (x, low, high) => Math.min(high, Math.max(low, x));

	const make = (tag, className, text) => {
		const node = document.createElement(tag);
		if (className)
			node.className = className;
		if (text !== undefined)
			node.textContent = text;
		return node;
	};

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

	const hueOf = (r, g, b, max, d) => {
		if (!d)
			return 0;
		const h = max === r ? ((g - b) / d) % 6 : max === g ? (b - r) / d + 2 : (r - g) / d + 4;
		return (h * 60 + 360) % 360;
	};

	const hsvToRgb = (h, s, v) => {
		const c = v * s;
		const x = c * (1 - Math.abs(((h / 60) % 2) - 1));
		const m = v - c;
		const [r, g, b] = [[c, x, 0], [x, c, 0], [0, c, x], [0, x, c], [x, 0, c], [c, 0, x]][Math.floor((((h % 360) + 360) % 360) / 60)];
		return [(r + m) * 255, (g + m) * 255, (b + m) * 255];
	};

	const rgbToHsv = (r, g, b) => {
		r /= 255;
		g /= 255;
		b /= 255;
		const max = Math.max(r, g, b);
		const d = max - Math.min(r, g, b);
		return [hueOf(r, g, b, max, d), max ? d / max : 0, max];
	};

	const rgbToHsl = (r, g, b) => {
		r /= 255;
		g /= 255;
		b /= 255;
		const max = Math.max(r, g, b);
		const min = Math.min(r, g, b);
		const d = max - min;
		const l = (max + min) / 2;
		return [hueOf(r, g, b, max, d), d ? d / (1 - Math.abs(2 * l - 1)) : 0, l];
	};

	const hslToRgb = (h, s, l) => {
		const v = l + s * Math.min(l, 1 - l);
		return hsvToRgb(h, v ? 2 * (1 - l / v) : 0, v);
	};

	const rgbToCmyk = (r, g, b) => {
		const k = 1 - Math.max(r, g, b) / 255;
		if (k === 1)
			return [0, 0, 0, 1];
		return [(1 - r / 255 - k) / (1 - k), (1 - g / 255 - k) / (1 - k), (1 - b / 255 - k) / (1 - k), k];
	};

	const cmykToRgb = (c, m, y, k) => [255 * (1 - c) * (1 - k), 255 * (1 - m) * (1 - k), 255 * (1 - y) * (1 - k)];

	/* the browser's own colour parser, through a canvas: names, hex, rgb(), hsl(), hwb() and so on */
	const probe = document.createElement("canvas").getContext("2d");

	const cssColor = (text) => {
		if (!probe || text.length > 64 || /^(?:currentcolor|inherit|initial|unset|revert)$/i.test(text))
			return null;
		probe.fillStyle = "#000";
		probe.fillStyle = text;
		const dark = probe.fillStyle;
		probe.fillStyle = "#fff";
		probe.fillStyle = text;
		if (dark !== probe.fillStyle)
			return null;
		const solid = /^#([0-9a-f]{2})([0-9a-f]{2})([0-9a-f]{2})$/.exec(dark);
		if (solid)
			return [parseInt(solid[1], 16), parseInt(solid[2], 16), parseInt(solid[3], 16), 1];
		const rgba = /^rgba?\(([^)]+)\)$/.exec(dark);
		if (!rgba)
			return null;
		const [r, g, b, a = 1] = rgba[1].split(",").map(Number);
		return [r, g, b, a];
	};

	/* rgb 255 255 255, hsl(210, 50%, 40%), hsv 30 80 90 and cmyk(0, 50, 100, 0), which CSS cannot always read */
	const notation = (text) => {
		const m = /^(rgba?|hsla?|hsv|hsb|cmyk)\s*\(?\s*([-\d.%\s,/]+?)\s*\)?$/i.exec(text.trim());
		const n = m && m[2].match(/-?\d*\.?\d+/g);
		if (!n || n.length > 4)
			return null;
		const v = n.map(Number);
		const alpha = (i) => (v.length > i ? clamp(v[i], 0, 1) : 1);
		switch (m[1].toLowerCase()) {
		case "rgb":
		case "rgba":
			return v.length >= 3 ? [...v.slice(0, 3).map((x) => clamp(x, 0, 255)), alpha(3)] : null;
		case "hsl":
		case "hsla":
			return v.length >= 3 ? [...hslToRgb(v[0], clamp(v[1], 0, 100) / 100, clamp(v[2], 0, 100) / 100), alpha(3)] : null;
		case "hsv":
		case "hsb":
			return v.length === 3 ? [...hsvToRgb(v[0], clamp(v[1], 0, 100) / 100, clamp(v[2], 0, 100) / 100), 1] : null;
		default:
			return v.length === 4 ? [...cmykToRgb(...v.map((x) => clamp(x, 0, 100) / 100)), 1] : null;
		}
	};

	const NUMBERS = /^[-\d.\s,%/]+$/;

	const BARE_HEX = /^[0-9a-f]{3,4}$|^[0-9a-f]{6}$|^[0-9a-f]{8}$/i;

	const parseColor = (text, bare) => {
		const s = text.trim();
		if (s.length > 64)
			return null;
		return cssColor(s) || notation(s) || (bare && BARE_HEX.test(s) ? cssColor(`#${s}`) : null);
	};

	const FILLER = ["color", "colour", "picker", "code", "codes", "hex", "html", "to", "convert", "converter", "of", "the", "a"];

	/* the colour a search asked for: "#f0f0f0", "color f0f0f0", "rgb 255 255 255", "coral color picker" */
	const fromQuery = (text) => {
		const s = text.trim().replace(/\s+/g, " ");
		const keyword = /^(?:color|colour|hex)\s+(.+)$/i.exec(s);
		let found = parseColor(keyword ? keyword[1] : s, !!keyword);
		if (found)
			return found;
		for (const pattern of [/#[0-9a-f]{3,8}\b/i, /\b(?:rgba?|hsla?|hsv|hsb|cmyk|hwb)\s*\([^)]*\)/i]) {
			const part = pattern.exec(s);
			found = part && parseColor(part[0], false);
			if (found)
				return found;
		}
		for (const word of s.split(" ").slice(0, 6)) {
			if (word.length > 2 && !FILLER.includes(word.toLowerCase()) && /^[a-z]+$/i.test(word) && (found = cssColor(word)))
				return found;
		}
		return null;
	};

	const state = { h: 210, s: 0.7, v: 0.9, a: 1 };

	const setRgb = (r, g, b, a) => {
		const [h, s, v] = rgbToHsv(r, g, b);
		state.h = s && v ? h : 0;
		state.s = s;
		state.v = v;
		state.a = clamp(a, 0, 1);
	};

	const rgbOf = () => hsvToRgb(state.h, state.s, state.v).map((x) => Math.round(clamp(x, 0, 255)));
	const alphaText = () => String(Number(state.a.toFixed(2)));
	const percent = (x) => `${Math.round(x * 100)}%`;
	const hex = (n) => n.toString(16).padStart(2, "0");

	const FIELDS = [
		{ id: "hex", label: "HEX", bare: true, text: () => {
			const [r, g, b] = rgbOf();
			return `#${hex(r)}${hex(g)}${hex(b)}${state.a < 1 ? hex(Math.round(state.a * 255)) : ""}`;
		} },
		{ id: "rgb", label: "RGB", text: () => [...rgbOf(), ...(state.a < 1 ? [alphaText()] : [])].join(", ") },
		{ id: "hsl", label: "HSL", text: () => {
			const [h, s, l] = rgbToHsl(...rgbOf());
			return [Math.round(s ? h : state.h), percent(s), percent(l), ...(state.a < 1 ? [alphaText()] : [])].join(", ");
		} },
		{ id: "hsv", label: "HSV", text: () => {
			const [h, s, v] = rgbToHsv(...rgbOf());
			return [Math.round(s && v ? h : state.h), percent(s), percent(v)].join(", ");
		} },
		{ id: "cmyk", label: "CMYK", text: () => rgbToCmyk(...rgbOf()).map(percent).join(", ") },
	];

	const card = make("section", "color");
	card.id = "colorpicker";
	card.dir = "ltr";
	card.setAttribute("aria-label", "Colour picker");

	const area = make("div", "color-area");
	area.tabIndex = 0;
	area.setAttribute("role", "slider");
	area.setAttribute("aria-label", "Saturation and brightness");
	const thumb = make("div", "color-thumb");
	const plane = make("canvas", "color-plane");
	area.append(plane, thumb);
	const painter = plane.getContext("2d");
	let image = null;
	let painted = NaN;

	/* the square is painted pixel by pixel: stacked CSS gradients are dithered, which looks like a grain */
	const paint = () => {
		if (!painter || !area.clientWidth || !area.clientHeight)
			return;
		let scale = Math.min(window.devicePixelRatio || 1, 2);
		if (area.clientWidth * area.clientHeight * scale * scale > 600000)
			scale = 1;
		const width = Math.round(area.clientWidth * scale);
		const height = Math.round(area.clientHeight * scale);
		if (plane.width !== width || plane.height !== height) {
			plane.width = width;
			plane.height = height;
			image = painter.createImageData(width, height);
			painted = NaN;
		}
		if (painted === state.h)
			return;
		painted = state.h;
		const [hr, hg, hb] = hsvToRgb(state.h, 1, 1);
		const data = image.data;
		for (let y = 0, i = 0; y < height; y++) {
			const v = 1 - y / (height - 1);
			for (let x = 0; x < width; x++, i += 4) {
				const s = x / (width - 1);
				data[i] = Math.round(v * (255 * (1 - s) + hr * s));
				data[i + 1] = Math.round(v * (255 * (1 - s) + hg * s));
				data[i + 2] = Math.round(v * (255 * (1 - s) + hb * s));
				data[i + 3] = 255;
			}
		}
		painter.putImageData(image, 0, 0);
		area.classList.add("painted");
	};
	new ResizeObserver(paint).observe(area);

	const swatchText = make("div", "color-swatch-text");
	const swatch = make("div", "color-swatch");
	swatch.append(swatchText);

	const slider = (name, max) => {
		const input = make("input", `color-slider color-${name}`);
		input.type = "range";
		input.min = "0";
		input.max = String(max);
		input.step = "1";
		input.setAttribute("aria-label", name === "hue" ? "Hue" : "Opacity");
		return input;
	};
	const hue = slider("hue", 360);
	const alpha = slider("alpha", 100);

	FIELDS.forEach((field) => {
		const input = make("input", "color-input");
		input.type = "text";
		input.setAttribute("autocomplete", "off");
		input.setAttribute("autocapitalize", "none");
		input.setAttribute("spellcheck", "false");
		input.setAttribute("aria-label", field.label);
		input.addEventListener("focus", () => input.select());
		field.input = input;
	});

	const render = (skip) => {
		const [r, g, b] = rgbOf();
		const luminance = (0.2126 * r + 0.7152 * g + 0.0722 * b) / 255;
		const set = (name, value) => card.style.setProperty(name, value);
		set("--hue", String(Math.round(state.h)));
		set("--sat", String(state.s * 100));
		set("--val", String(state.v * 100));
		set("--solid", `rgb(${r}, ${g}, ${b})`);
		set("--color", `rgba(${r}, ${g}, ${b}, ${state.a})`);
		set("--contrast", luminance > 0.58 || state.a < 0.4 ? "#14141c" : "#ffffff");
		paint();
		hue.value = String(Math.round(state.h));
		alpha.value = String(Math.round(state.a * 100));
		area.setAttribute("aria-valuetext", `Saturation ${percent(state.s)}, brightness ${percent(state.v)}`);
		for (const field of FIELDS) {
			if (field.input !== skip)
				field.input.value = field.text();
		}
		swatchText.textContent = FIELDS[0].text();
	};

	const move = (event) => {
		const box = area.getBoundingClientRect();
		state.s = clamp((event.clientX - box.left) / box.width, 0, 1);
		state.v = 1 - clamp((event.clientY - box.top) / box.height, 0, 1);
		render();
	};

	area.addEventListener("pointerdown", (event) => {
		area.setPointerCapture(event.pointerId);
		area.focus({ preventScroll: true });
		move(event);
	});
	area.addEventListener("pointermove", (event) => {
		if (area.hasPointerCapture(event.pointerId))
			move(event);
	});
	area.addEventListener("keydown", (event) => {
		const step = event.shiftKey ? 0.1 : 0.01;
		const change = new Map([["ArrowLeft", [-step, 0]], ["ArrowRight", [step, 0]], ["ArrowUp", [0, step]],
			["ArrowDown", [0, -step]]]).get(event.key);
		if (!change)
			return;
		event.preventDefault();
		state.s = clamp(state.s + change[0], 0, 1);
		state.v = clamp(state.v + change[1], 0, 1);
		render();
	});
	hue.addEventListener("input", () => {
		state.h = Number(hue.value);
		render();
	});
	alpha.addEventListener("input", () => {
		state.a = Number(alpha.value) / 100;
		render();
	});

	FIELDS.forEach((field) => {
		field.input.addEventListener("input", () => {
			const text = field.input.value;
			const found = !field.bare && NUMBERS.test(text) ? notation(`${field.id}(${text})`) : parseColor(text, field.bare);
			field.input.setAttribute("aria-invalid", String(!found));
			if (found) {
				setRgb(...found);
				render(field.input);
			}
		});
		field.input.addEventListener("change", () => {
			field.input.setAttribute("aria-invalid", "false");
			render();
		});
	});

	const upper = make("div", "color-top");
	upper.append(area, swatch);
	const sliders = make("div", "color-sliders");
	sliders.append(hue, alpha);

	const [hexField, ...others] = FIELDS;
	const hexRow = make("label", "color-hex");
	hexRow.append(make("span", "color-name", hexField.label), hexField.input);

	if ("EyeDropper" in window) {
		const svg = (tag, attributes) => {
			const node = document.createElementNS("http://www.w3.org/2000/svg", tag);
			for (const [name, value] of Object.entries(attributes))
				node.setAttribute(name, value);
			return node;
		};
		const icon = svg("svg", { viewBox: "0 0 24 24", "aria-hidden": "true", fill: "none", stroke: "currentColor",
			"stroke-width": "2", "stroke-linecap": "round", "stroke-linejoin": "round" });
		icon.append(svg("path", { d: "m2 22 1-1h3l9-9" }), svg("path", { d: "M3 21v-3l9-9" }),
			svg("path", { d: "m15 6 3.4-3.4a2.1 2.1 0 1 1 3 3L18 9l.4.4a2.1 2.1 0 1 1-3 3l-3.8-3.8a2.1 2.1 0 1 1 3-3l.4.4Z" }));
		const dropper = make("button", "color-pick");
		dropper.type = "button";
		dropper.title = "Pick a colour from the screen";
		dropper.setAttribute("aria-label", "Pick a colour from the screen");
		dropper.append(icon);
		dropper.addEventListener("click", () => {
			new window.EyeDropper().open().then((picked) => {
				const found = cssColor(picked.sRGBHex);
				if (found) {
					setRgb(...found);
					render();
				}
			}, () => {});
		});
		hexRow.append(dropper);
	}

	const boxes = make("div", "color-boxes");
	for (const field of others) {
		const box = make("label", "color-box");
		box.append(make("span", "color-name", field.label), field.input);
		boxes.append(box);
	}
	card.append(upper, sliders, hexRow, boxes);

	const style = getComputedStyle(document.documentElement).getPropertyValue("--accent").trim();
	const start = fromQuery(query.value) || cssColor(style) || [91, 141, 239, 1];
	setRgb(...start);
	render();
	showWithResults(card);
})();
