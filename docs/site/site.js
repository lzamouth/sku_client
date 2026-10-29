/* The two link pages: the theme the visitor picked (system, light, dark), remembered in this
 * browser, and the current version read from the client's depot configuration. */
(function () {
	var KEY = "sku-theme";
	function apply(t) {
		if (t === "light" || t === "dark") document.documentElement.setAttribute("data-theme", t);
		else document.documentElement.removeAttribute("data-theme");
	}
	var saved = "system";
	try { saved = localStorage.getItem(KEY) || "system"; } catch (e) {}
	apply(saved);   /* before the page is drawn: no flash of the wrong theme */

	document.addEventListener("DOMContentLoaded", function () {
		var sel = document.getElementById("theme");
		if (sel) {
			sel.value = saved;
			sel.addEventListener("change", function () {
				apply(sel.value);
				try { localStorage.setItem(KEY, sel.value); } catch (e) {}
			});
		}
		fetch("depot/skunkcrafts_updater.cfg", { cache: "no-store" })
			.then(function (r) { return r.ok ? r.text() : ""; })
			.then(function (text) {
				var m = /^version\|(.+)$/m.exec(text);
				if (!m) return;
				document.querySelectorAll("[data-version]").forEach(function (b) { b.textContent = m[1].trim(); });
				document.querySelectorAll(".version").forEach(function (s) { s.hidden = false; });
			})
			.catch(function () {});
	});
})();
