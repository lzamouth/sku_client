#!/usr/bin/env python3
"""Markdown -> standalone HTML page (python-markdown), for the user guide shipped in the packages.
usage: md2html.py <in.md> <out.html>"""
import sys, html, markdown

src, dst = sys.argv[1], sys.argv[2]
text = open(src, encoding="utf-8").read()
title = next((l[2:].strip() for l in text.splitlines() if l.startswith("# ")), "SkunkCrafts Updater")
body = markdown.markdown(text, extensions=["tables", "fenced_code", "toc"])
page = """<!DOCTYPE html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>%s</title>
<style>
body { font-family: -apple-system, "Segoe UI", Roboto, Helvetica, Arial, sans-serif; max-width: 52em; margin: 2em auto; padding: 0 1em; line-height: 1.5; color: #222; background: #fff; }
h1, h2, h3 { line-height: 1.25; } h1 { border-bottom: 2px solid #ddd; padding-bottom: .3em; } h2 { margin-top: 2em; border-bottom: 1px solid #eee; }
code { font-family: Consolas, "DejaVu Sans Mono", Menlo, monospace; background: #f3f3f3; padding: .1em .3em; border-radius: 3px; font-size: .95em; }
pre { background: #f3f3f3; padding: .8em 1em; overflow-x: auto; border-radius: 4px; } pre code { background: none; padding: 0; }
table { border-collapse: collapse; margin: 1em 0; } th, td { border: 1px solid #ccc; padding: .35em .6em; text-align: left; vertical-align: top; } th { background: #f3f3f3; }
@media (prefers-color-scheme: dark) { body { color: #ddd; background: #1e1e1e; } code, pre, th { background: #2b2b2b; } th, td { border-color: #444; } h1 { border-color: #444; } h2 { border-color: #333; } }
</style></head><body>
%s
</body></html>
""" % (html.escape(title), body)
open(dst, "w", encoding="utf-8").write(page)
