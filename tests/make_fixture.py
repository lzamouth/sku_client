#!/usr/bin/env python3
"""Builds an end-to-end fixture: a fake X-Plane root with one 3.x addon, and a
depot (level 1 and level 2 variants) to serve over HTTP.

usage: make_fixture.py <work-dir> <port>
creates <work-dir>/xp (X-Plane root) and <work-dir>/depot (to serve)."""
import os, sys, zlib

work = sys.argv[1]
port = sys.argv[2]
xp = os.path.join(work, "xp")
depot = os.path.join(work, "depot")

def write(path, data):
	os.makedirs(os.path.dirname(path), exist_ok=True)
	mode = "wb" if isinstance(data, bytes) else "w"
	with open(path, mode) as f:
		f.write(data)

def crc(data):
	return zlib.crc32(data) & 0xFFFFFFFF

# --- X-Plane root
for d in ["Aircraft", "Custom Scenery", "Resources/plugins", "Output/preferences"]:
	os.makedirs(os.path.join(xp, d), exist_ok=True)

addon = os.path.join(xp, "Aircraft", "Test", "Plane")
write(os.path.join(addon, "skunkcrafts_updater.cfg"),
	"name|Test plane\nmodule|http://127.0.0.1:" + port + "/level1\nversion|1.0\nlocked|false\ndisabled|false\nliveries|true\nzone|custom\n")
write(os.path.join(addon, "skunkcrafts_updater_beta.cfg"),
	"installed|true\nenabled|false\nmodule|http://127.0.0.1:" + port + "/beta\nzone|custom\n")
write(os.path.join(addon, "skunkcrafts_updater_ignore.txt"), "data/user.prf\n")
write(os.path.join(addon, "plane.acf"), b"ACF v1")                     # unchanged
write(os.path.join(addon, "objects/wing.obj"), b"old wing")           # changed
write(os.path.join(addon, "objects/obsolete.png"), b"to be deleted")  # blacklisted
write(os.path.join(addon, "data/user.prf"), b"user data")             # ignored
write(os.path.join(addon, "liveries/Red/tex.png"), b"old livery")     # changed, in liveries

# a pack: two scenery members present, a third one declared by A but missing
pack_files = {"a": {"Earth nav data/apt.dat": b"pack A data", "library.txt": b"A"},
              "b": {"objects/tree.obj": b"pack B tree"},
              "c": {"objects/house.obj": b"pack C house", "library.txt": b"C"}}
members_decl = ";".join("%s=Custom Scenery/Pack %s=http://127.0.0.1:%s/pack%s" % (m, m.upper(), port, m) for m in ("a", "b", "c"))
for m in ("a", "b"):
	folder = os.path.join(xp, "Custom Scenery", "Pack " + m.upper())
	cfg = "name|Pack member %s\nmodule|http://127.0.0.1:%s/pack%s\nversion|1.0\nzone|custom\npack|simpack\nmember|%s\npack_name|Sim Pack\n" % (m, port, m, m)
	if m == "a":
		cfg += "pack_members|" + members_decl + "\n"
	write(os.path.join(folder, "skunkcrafts_updater.cfg"), cfg)
	for rel, data in pack_files[m].items():
		write(os.path.join(folder, rel), b"old " + data)
for m in ("a", "b", "c"):
	base = os.path.join(depot, "pack" + m)
	for rel, data in pack_files[m].items():
		write(os.path.join(base, rel), data)
	# the depot cfg carries the pack identity too: an installed member adopts it at the first check
	write(os.path.join(base, "skunkcrafts_updater.cfg"), "name|Pack member %s\nmodule|http://127.0.0.1:%s/pack%s\nversion|2.0\nlocked|false\npack|simpack\nmember|%s\npack_name|Sim Pack\npack_members|%s\n" % (m, port, m, m, members_decl))
	write(os.path.join(base, "skunkcrafts_updater_whitelist.txt"), "".join("%s|%d\n" % (rel, crc(data)) for rel, data in pack_files[m].items()))

# a second folder that must not be scanned as an addon (a .acf stops the walk)
write(os.path.join(xp, "Aircraft", "Other", "other.acf"), b"x")
write(os.path.join(xp, "Aircraft", "Other", "deep", "skunkcrafts_updater.cfg"), "name|hidden\nmodule|x\nversion|1\n")

# --- depot: the same files at version 2.0
files = {
	"plane.acf": b"ACF v1",
	"objects/wing.obj": b"new wing v2",
	"objects/new.obj": b"brand new object",
	"data/user.prf": b"depot user data",
	"data/OptionSave.prf": b"whatever",          # crc -1: never checked
	"liveries/Red/tex.png": b"new livery",
	"liveries/Blue/tex.png": b"blue livery",
	"sub dir/with space.txt": b"spaces in names",
	"changelog.txt": b"2.0\n- new wing (level 1 style, bare version headers)\n\n1.0\n- first\n",
}
for level in ("level1", "level2"):
	base = os.path.join(depot, level)
	for rel, data in files.items():
		write(os.path.join(base, rel), data)
	cfg = "name|Test plane\nmodule|http://127.0.0.1:" + port + "/" + level + "\nversion|2.0\nlocked|false\nzone|custom\n"
	if level == "level2":
		cfg += "protocol|2\nchangelog|docs/CHANGELOG.md\nreadme|readme.txt\n"
		write(os.path.join(base, "docs/CHANGELOG.md"), "# Test plane\n\n## 2.0\n- new wing\n- blue livery\n\n## 1.5\n- interim\n\n## 1.0\n- first\n")
		write(os.path.join(base, "readme.txt"), "Test plane readme\nRead me first.\n")
		lines = []
		for rel, data in files.items():
			c = -1 if rel == "data/OptionSave.prf" else crc(data)
			lines.append("%s|%d|%d" % (rel, c, len(data)))
		lines.append("../../evil|1|1")
		write(os.path.join(base, "skunkcrafts_updater_manifest.txt"), "\n".join(lines) + "\n")
	else:
		wl, sz = [], []
		for rel, data in files.items():
			c = -1 if rel == "data/OptionSave.prf" else crc(data)
			wl.append("%s|%d" % (rel, c))
			sz.append("%s|%d" % (rel, len(data)))
		wl.append("../../evil|1")
		write(os.path.join(base, "skunkcrafts_updater_whitelist.txt"), "\n".join(wl) + "\n")
		write(os.path.join(base, "skunkcrafts_updater_sizeslist.txt"), "\n".join(sz) + "\n")
	write(os.path.join(base, "skunkcrafts_updater.cfg"), cfg)
	write(os.path.join(base, "skunkcrafts_updater_blacklist.txt"), "objects/obsolete.png\r\n../../etc/passwd\r\n")

# the updater's own depot: fixed package names, at two versions
packages = ["SkunkcraftsUpdater-windows-setup.exe", "SkunkcraftsUpdater-windows.zip", "SkunkcraftsUpdater-macos.zip",
            "SkunkcraftsUpdater-linux.zip", "SkunkcraftsUpdater-x86_64.AppImage", "sku-cli.zip"]
for name, version in (("client", "9.9.9"), ("client_old", "0.1.0")):
	base = os.path.join(depot, name)
	wl, sz = [], []
	for p in packages:
		data = ("package " + p + " " + version).encode()
		write(os.path.join(base, p), data)
		wl.append("%s|%d" % (p, crc(data)))
		sz.append("%s|%d" % (p, len(data)))
	write(os.path.join(base, "skunkcrafts_updater.cfg"), "zone|custom\nmodule|http://127.0.0.1:%s/%s\nname|SkunkCrafts Updater\nversion|%s\nlocked|false\n" % (port, name, version))
	write(os.path.join(base, "skunkcrafts_updater_whitelist.txt"), "\n".join(wl) + "\n")
	write(os.path.join(base, "skunkcrafts_updater_sizeslist.txt"), "\n".join(sz) + "\n")
	write(os.path.join(base, "changelog.txt"), version + "\n- self update\n\n0.0.1\n- first\n")

# the beta depot: same files as level 1, the same cfg (production URL) at a beta version
base = os.path.join(depot, "beta")
for rel, data in files.items():
	write(os.path.join(base, rel), data)
write(os.path.join(base, "skunkcrafts_updater.cfg"), "name|Test plane\nmodule|http://127.0.0.1:" + port + "/level1\nversion|2.0b1\nlocked|false\nzone|custom\n")
write(os.path.join(base, "skunkcrafts_updater_whitelist.txt"), "\n".join("%s|%d" % (rel, -1 if rel == "data/OptionSave.prf" else crc(data)) for rel, data in files.items()) + "\n")

# a locked depot and one answering HTML instead of the cfg
write(os.path.join(depot, "locked", "skunkcrafts_updater.cfg"), "name|Test plane\nmodule|x\nversion|3.0\nlocked|true\n")
write(os.path.join(depot, "html", "skunkcrafts_updater.cfg"), "<!DOCTYPE html><html><body>403</body></html>")
print("fixture written to", work)
