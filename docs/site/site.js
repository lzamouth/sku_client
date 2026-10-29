/* The two link pages: the theme the visitor picked (system, light, dark), remembered in this
 * browser, and the current version, from version.js (written by publish.sh at every release;
 * a script, so it loads from the CDN even when the page is served by another site). */
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
		if (window.SKU_VERSION) {
			document.querySelectorAll("[data-version]").forEach(function (b) { b.textContent = window.SKU_VERSION; });
			document.querySelectorAll(".version").forEach(function (s) { s.hidden = false; });
		}
	});
})();
