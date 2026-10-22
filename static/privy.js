"use strict";

(() => {
	const form = document.getElementById("search");
	const q = document.getElementById("q");
	const box = document.querySelector(".autocomplete");
	const list = document.querySelector(".autocomplete ul");
	const send = document.getElementById("send_search");
	const searchIcon = send && send.querySelector("svg");
	const language = document.querySelector("select[name=language]");
	const MIN_LENGTH = 2;

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
					const results = doc.querySelectorAll("#urls > article.result");
					for (const article of results)
						urls.append(document.adoptNode(article));
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

	const hideBroken = (img) => {
		const target = img.closest(".result-images") || img.closest(".thumbnail_link") || img;
		if (target.matches(".result-images"))
			target.remove();
		else
			target.hidden = true;
	};

	const brokenImages = () => {
		for (const img of document.querySelectorAll("img.thumbnail, .image_thumb img, .infobox > img"))
			if (img.complete && !img.naturalWidth)
				hideBroken(img);
	};

	document.addEventListener("error", (event) => {
		if (event.target.matches && event.target.matches("img.thumbnail, .image_thumb img, .infobox > img"))
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
			[...document.querySelectorAll("#urls > article.result-images")]
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
			for (const [label, value] of [["Resolution", d.resolution], ["Format", d.format], ["Engines", d.engines]]) {
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
			article.scrollIntoView({ block: "nearest" });
		};

		const close = () => {
			if (!panel)
				return;
			panel.hidden = true;
			document.documentElement.classList.remove("image_detail_open");
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
	let trending = null;

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
			img.src = icon;
			img.alt = "";
			img.decoding = "async";
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

	const display = (items, id) => {
		if (suppress || id <= displayed || !items.length)
			return;
		displayed = id;
		box.classList.add("open");
		const fragment = new DocumentFragment();
		for (const item of items)
			if (text(item))
				fragment.append(render(item, q.value));
		list.replaceChildren(fragment);
	};

	const fetchResults = (query, id) => {
		if (query.length === 0 && trending) {
			display(trending, id);
			return;
		}
		let url = "/autocompleter?q=" + encodeURIComponent(query);
		if (language)
			url += "&language=" + encodeURIComponent(language.value);
		fetch(url, { headers: { Accept: "application/json" }, cache: "no-store" })
			.then((res) => {
				if (!res.ok)
					throw new Error(res.status + " " + res.statusText);
				return res.json();
			})
			.then((results) => {
				const items = (results && results[1]) || [];
				if (query.length === 0 && items.length)
					trending = items;
				display(items, id);
			})
			.catch(() => {});
	};

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

	q.addEventListener("blur", hide);

	q.addEventListener("focus", () => {
		suppress = false;
		if (list.children.length) {
			box.classList.add("open");
			return;
		}
		if (shouldFetch(q.value))
			schedule(q.value);
	});
})();
