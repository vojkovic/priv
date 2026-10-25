"use strict";

(() => {
	const form = document.getElementById("search");
	const q = document.getElementById("q");
	const box = document.querySelector(".autocomplete");
	const list = document.querySelector(".autocomplete ul");
	const send = document.getElementById("send_search");
	const searchIcon = send && send.querySelector("svg");
	const language = document.querySelector("select[name=language]");
	const MIN_LENGTH = 1;

	if (!form || !q)
		return;

	const submit = () => (form.requestSubmit ? form.requestSubmit(send) : form.submit());

	for (const select of document.querySelectorAll(".search_filters select"))
		select.addEventListener("change", submit);

	const infiniteScroll = () => {
		const urls = document.getElementById("urls");
		let next = document.querySelector("#pagination .next_page");
		if (!urls || !next || !("IntersectionObserver" in window))
			return;
		document.documentElement.classList.add("infinite_scroll");
		const sentinel = document.createElement("div");
		sentinel.className = "infinite_loader";
		urls.after(sentinel);
		let busy = false;
		const observer = new IntersectionObserver((entries) => {
			if (busy || !next || !entries.some((e) => e.isIntersecting))
				return;
			busy = true;
			sentinel.classList.add("active");
			fetch(next.href, { credentials: "same-origin" })
				.then((res) => res.text())
				.then((html) => {
					const doc = new DOMParser().parseFromString(html, "text/html");
					const results = doc.querySelectorAll("#urls article.result");
					for (const article of results)
						urls.append(document.adoptNode(article));
					const sprite = doc.querySelector("svg.sprite");
					if (sprite)
						document.body.append(document.adoptNode(sprite));
					refit();
					next = results.length ? doc.querySelector("#pagination .next_page") : null;
				})
				.catch(() => {
					next = null;
				})
				.finally(() => {
					busy = false;
					sentinel.classList.remove("active");
					if (!next) {
						observer.disconnect();
						sentinel.remove();
					} else {
						observer.unobserve(sentinel);
						observer.observe(sentinel);
					}
				});
		}, { rootMargin: "0px 0px 600px 0px" });
		observer.observe(sentinel);
	};

	if (document.readyState === "loading")
		document.addEventListener("DOMContentLoaded", infiniteScroll);
	else
		infiniteScroll();

	/* widen images in rows the grid packing left short, so every row but the last reaches both edges */
	const fillRows = () => {
		const urls = document.querySelector(".category-images #urls");
		if (!urls)
			return;
		const items = [...urls.querySelectorAll(":scope > article.result-images")];
		for (const el of items)
			el.style.removeProperty("grid-column");
		const columns = getComputedStyle(urls).gridTemplateColumns.split(" ").length;
		const span = (el) => parseInt(el.style.gridColumn.replace("span ", ""), 10) ||
			parseInt(getComputedStyle(el).getPropertyValue("--s"), 10);
		const card = urls.querySelector(":scope > #engines_msg");
		for (let pass = 0; pass < items.length; pass++) {
			const rows = new Map();
			for (const el of items) {
				const top = el.offsetTop;
				if (!rows.has(top))
					rows.set(top, []);
				rows.get(top).push(el);
			}
			const tops = [...rows.keys()].sort((x, y) => x - y);
			const short = tops.slice(0, -1).find((top) => {
				const used = rows.get(top).reduce((sum, el) => sum + span(el), 0) +
					(card && card.offsetTop === top && columns > 14 ? 7 : 0);
				return used < columns;
			});
			if (short === undefined)
				break;
			const row = rows.get(short);
			let free = columns - row.reduce((sum, el) => sum + span(el), 0) -
				(card && card.offsetTop === short && columns > 14 ? 7 : 0);
			for (let i = 0; free > 0; i = (i + 1) % row.length, free--)
				row[i].style.gridColumn = "span " + (span(row[i]) + 1);
		}
	};

	let fitting = 0;
	const refit = () => {
		cancelAnimationFrame(fitting);
		fitting = requestAnimationFrame(fillRows);
	};
	window.addEventListener("resize", refit);
	if (document.readyState === "loading")
		document.addEventListener("DOMContentLoaded", fillRows);
	else
		fillRows();

	const hideBroken = (img) => {
		const target = img.closest(".result-images") || img.closest(".thumbnail_link") || img;
		if (target.matches(".result-images")) {
			target.remove();
			refit();
		}
		else
			target.hidden = true;
	};

	const proxied = "img.thumbnail, .image_thumb img, .infobox > img";
	/* the proxy answers failed images with a placeholder of this size */
	const failed = (img) => (img.complete && !img.naturalWidth) || (img.naturalWidth === 403 && img.naturalHeight === 302);

	const brokenImages = () => {
		for (const img of document.querySelectorAll(proxied))
			if (failed(img))
				hideBroken(img);
	};

	for (const type of ["error", "load"])
		document.addEventListener(type, (event) => {
			if (event.target.matches && event.target.matches(proxied) && failed(event.target))
				hideBroken(event.target);
		}, true);

	if (document.readyState === "loading")
		document.addEventListener("DOMContentLoaded", brokenImages);
	else
		brokenImages();

	const imageDetail = () => {
		const template = document.getElementById("image_detail");
		let panel = null;
		let current = null;

		if (!template)
			return;

		const $ = (selector) => panel.querySelector(selector);
		const articles = () =>
			[...document.querySelectorAll("#urls article.result-images")]
				.map((el) => ({ el, rect: el.getBoundingClientRect() }))
				.sort((x, y) => Math.round(x.rect.top - y.rect.top) || x.rect.left - y.rect.left)
				.map((x) => x.el);

		const build = () => {
			panel = template.content.firstElementChild.cloneNode(true);
			panel.hidden = true;
			$(".detail_prev").addEventListener("click", () => step(-1));
			$(".detail_next").addEventListener("click", () => step(1));
			$(".detail_close").addEventListener("click", close);
			document.body.append(panel);
		};

		const open = (article) => {
			if (!panel)
				build();
			if (current)
				current.classList.remove("selected");
			current = article;
			article.classList.add("selected");
			const d = article.dataset;
			const full = article.querySelector(".image_thumb").href;
			const thumb = article.querySelector(".image_thumb img");
			const img = $(".detail_image img");
			img.src = thumb ? thumb.src : full;
			img.alt = d.title;
			const loader = new Image();
			loader.onload = () => {
				if (current === article)
					img.src = full;
			};
			loader.src = full;
			$(".detail_image").href = full;
			$(".detail_site").href = d.url;
			$(".detail_site img").src = d.favicon;
			$(".detail_site img").hidden = !d.favicon;
			$(".detail_site span").textContent = d.site;
			$(".detail_title").href = d.url;
			$(".detail_title").textContent = d.title || d.site;
			$(".detail_content").textContent = d.content;
			const dl = $("dl");
			dl.replaceChildren();
			const l = dl.dataset;
			for (const [label, value] of [[l.resolution, d.resolution], [l.format, d.format], [l.engines, d.engines]]) {
				if (!value)
					continue;
				const dt = document.createElement("dt");
				const dd = document.createElement("dd");
				dt.textContent = label;
				dd.textContent = value;
				dl.append(dt, dd);
			}
			const all = articles();
			const i = all.indexOf(article);
			$(".detail_prev").disabled = i <= 0;
			$(".detail_next").disabled = i < 0 || i >= all.length - 1;
			panel.hidden = false;
			document.documentElement.classList.add("image_detail_open");
			refit();
			article.scrollIntoView({ block: "nearest" });
		};

		const close = () => {
			if (!panel)
				return;
			panel.hidden = true;
			document.documentElement.classList.remove("image_detail_open");
			refit();
			if (current)
				current.classList.remove("selected");
			current = null;
		};

		const step = (delta) => {
			const all = articles();
			const next = all[all.indexOf(current) + delta];
			if (current && next)
				open(next);
		};

		document.addEventListener("click", (event) => {
			const link = event.target.closest && event.target.closest("a.image_thumb");
			if (!link || event.button !== 0 || event.metaKey || event.ctrlKey || event.shiftKey || event.altKey)
				return;
			event.preventDefault();
			link.blur();
			open(link.closest("article"));
		});

		document.addEventListener("keydown", (event) => {
			if (!current || event.target.matches("input, select, textarea"))
				return;
			if (event.key === "Escape")
				close();
			else if (event.key === "ArrowLeft")
				step(-1);
			else if (event.key === "ArrowRight")
				step(1);
			else
				return;
			event.preventDefault();
		});
	};

	imageDetail();

	if (!box || !list)
		return;

	let suppress = false;
	let counter = 0;
	let displayed = 0;
	let shown = null;

	const text = (item) => (typeof item === "string" ? item : item.text || "");
	const rich = (item) => (typeof item === "string" ? {} : item);
	const shouldFetch = (query) => query.length === 0 || query.length >= MIN_LENGTH || /(^|\s)[!:<][^\s]*$/.test(query);

	const fill = (el, suggestion, query) => {
		const words = query.toLowerCase().split(/\s+/).filter(Boolean);
		if (!words.length) {
			el.textContent = suggestion;
			return;
		}
		const typed = words[words.length - 1];
		const complete = new Set(words.slice(0, -1));
		const bold = (s) => {
			if (!s)
				return;
			const b = document.createElement("b");
			b.textContent = s;
			el.append(b);
		};
		for (const token of suggestion.split(/(\s+)/)) {
			const lower = token.toLowerCase();
			if (token === "" || /\s/.test(token) || complete.has(lower)) {
				el.append(token);
			} else if (lower.startsWith(typed)) {
				el.append(token.slice(0, typed.length));
				bold(token.slice(typed.length));
			} else {
				bold(token);
			}
		}
	};

	const svg = (d) => {
		const s = document.createElementNS("http://www.w3.org/2000/svg", "svg");
		s.setAttribute("viewBox", "0 0 24 24");
		s.setAttribute("aria-hidden", "true");
		const p = document.createElementNS("http://www.w3.org/2000/svg", "path");
		p.setAttribute("d", d);
		s.append(p);
		return s;
	};

	const iconSlot = (item) => {
		const { icon, description, trending: hot } = rich(item);
		const slot = document.createElement("span");
		slot.className = "autocomplete-icon-slot";
		if (description)
			slot.classList.add("is-entity");
		if (hot)
			slot.classList.add("is-trending");
		if (icon) {
			const img = document.createElement("img");
			img.className = "autocomplete-icon-image";
			img.alt = "";
			img.decoding = "async";
			const drop = () => {
				const span = document.createElement("span");
				span.className = "autocomplete-icon-fallback";
				span.setAttribute("aria-hidden", "true");
				if (searchIcon)
					span.append(searchIcon.cloneNode(true));
				slot.classList.remove("is-entity");
				img.replaceWith(span);
			};
			img.addEventListener("error", drop);
			img.addEventListener("load", () => {
				if (img.naturalWidth === 403 && img.naturalHeight === 302)
					drop();
			});
			img.src = icon;
			slot.append(img);
		} else {
			const span = document.createElement("span");
			span.setAttribute("aria-hidden", "true");
			if (hot) {
				span.className = "autocomplete-icon-trending";
				span.append(svg("M16 6l2.29 2.29-4.88 4.88-4-4L2 16.59 3.41 18l6-6 4 4 6.3-6.29L22 12V6z"));
			} else {
				span.className = "autocomplete-icon-fallback";
				if (searchIcon)
					span.append(searchIcon.cloneNode(true));
			}
			slot.append(span);
		}
		return slot;
	};

	const hide = () => {
		suppress = true;
		box.classList.remove("open");
	};

	const close = () => {
		hide();
		list.replaceChildren();
	};

	const render = (item, query) => {
		const li = document.createElement("li");
		const content = document.createElement("div");
		const title = document.createElement("span");
		const description = rich(item).description;

		content.className = "autocomplete-content";
		title.className = "autocomplete-text";
		fill(title, text(item), query);
		content.append(title);
		if (description) {
			const sub = document.createElement("span");
			sub.className = "autocomplete-description";
			sub.textContent = description;
			content.append(sub);
		}
		li.append(iconSlot(item), content);
		li.addEventListener("mousedown", () => {
			q.value = text(item);
			submit();
		});
		return li;
	};

	const fit = () => box.style.setProperty("--room", Math.max(160, window.innerHeight - box.parentElement.getBoundingClientRect().bottom - 16) + "px");
	window.addEventListener("resize", fit);

	const display = (items, id, query) => {
		if (suppress || id <= displayed)
			return;
		displayed = id;
		/* nothing for this query, so the last list would be stale */
		if (!items.length) {
			box.classList.remove("open");
			list.replaceChildren();
			shown = null;
			return;
		}
		shown = query;
		fit();
		box.classList.add("open");
		const fragment = new DocumentFragment();
		for (const item of items)
			if (text(item))
				fragment.append(render(item, q.value));
		list.replaceChildren(fragment);
	};

	const seen = new Map();

	const load = (query) => {
		let url = "/autocompleter?q=" + encodeURIComponent(query);
		if (language)
			url += "&language=" + encodeURIComponent(language.value);
		if (!seen.has(url))
			seen.set(
				url,
				fetch(url, { headers: { Accept: "application/json" }, cache: "no-store" })
					.then((res) => {
						if (!res.ok)
							throw new Error(res.status + " " + res.statusText);
						return res.json();
					})
					.then((results) => (results && results[1]) || [])
					.catch((err) => {
						seen.delete(url);
						throw err;
					}),
			);
		return seen.get(url);
	};

	const fetchResults = (query, id) => {
		load(query)
			.then((items) => display(items, id, query))
			.catch(() => {});
	};

	(window.requestIdleCallback || ((cb) => setTimeout(cb, 200)))(() => load("").catch(() => {}));

	const schedule = (query) => {
		if (!shouldFetch(query)) {
			close();
			return;
		}
		suppress = false;
		fetchResults(query, ++counter);
	};

	q.addEventListener("input", () => schedule(q.value));

	q.addEventListener("keydown", (event) => {
		if (event.key === "Escape")
			close();
	});

	q.addEventListener("keyup", (event) => {
		const items = [...list.children];
		if (!items.length) {
			if (event.key === "Enter")
				close();
			return;
		}
		const current = items.findIndex((item) => item.classList.contains("active"));
		if (current >= 0)
			items[current].classList.remove("active");
		let next = -1;
		if (event.key === "ArrowUp")
			next = (current - 1 + items.length) % items.length;
		else if (event.key === "ArrowDown")
			next = (current + 1) % items.length;
		else if (event.key === "Enter") {
			close();
			return;
		}
		if (next === -1)
			return;
		items[next].classList.add("active");
		q.value = items[next].querySelector(".autocomplete-text").textContent;
	});

	const phone = window.matchMedia("(max-width: 41.25em)");
	const back = document.getElementById("search_back");

	if ("autofocus" in q.dataset && window.matchMedia("(pointer: fine)").matches)
		q.focus();

	q.addEventListener("blur", () => {
		hide();
		document.documentElement.classList.remove("search_focus");
	});

	if (back)
		back.addEventListener("pointerdown", (event) => {
			event.preventDefault();
			q.blur();
		});

	q.addEventListener("focus", () => {
		if (phone.matches)
			document.documentElement.classList.add("search_focus");
		suppress = false;
		if (list.children.length && shown === q.value) {
			fit();
			box.classList.add("open");
			return;
		}
		if (shouldFetch(q.value))
			schedule(q.value);
	});
})();
