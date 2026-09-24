# Publishing a multi-folder product as a "pack"

SkunkCrafts Updater 4.0 - guide for developers whose product is installed as
several folders (for example a scenery library plus several regional scenery
folders). Written for simHeaven, applies to any developer.

## 1. What a pack is

Users see one entry in the updater instead of one per folder: the pack name,
one version, one status, and one "Update" button that updates every folder.
Each folder remains an ordinary updater depot, exactly as today: its own
`skunkcrafts_updater.cfg`, its own file list, its own files, its own URL. A pack
is nothing more than a few extra lines in those cfg files that tell the client
which folders belong together.

Nothing changes for users of the 3.x client: they keep seeing one entry per
folder, and update them one by one, as before. The same depots serve both.

Users rename scenery folders (to sort `scenery_packs.ini`) and move things
around. The client therefore never relies on folder names: members are
recognised by an identity written in their cfg, wherever the folder is.

## 2. One depot per member

Publish each folder as a normal depot, with `xp_depot` or by hand, at any URL,
in any zone. Per member folder, on the server:

```
skunkcrafts_updater.cfg               (uploaded from skunkcrafts_updater_config.txt)
skunkcrafts_updater_whitelist.txt     path|crc32        (generated)
skunkcrafts_updater_sizeslist.txt     path|bytes        (generated)
skunkcrafts_updater_blacklist.txt     files to delete   (optional)
changelog.txt                         optional, see section 6
... the files of that folder
```

The file lists and the lock work per member. Uploading member A does not lock
member B; a user who checks for updates while you upload A sees A locked and B
up to date, which is harmless.

## 3. The cfg of a member

A member's `skunkcrafts_updater.cfg` is the usual one plus three keys:

| Key          | Required | Meaning |
|--------------|----------|---------|
| `name`       | yes      | display name of this folder (the usual key) |
| `module`     | yes      | URL of this folder's depot (the usual key) |
| `version`    | yes      | version of this folder (the usual key) |
| `locked`     | no       | `true` while an upload is running (the usual key) |
| `pack`       | yes      | identifier of the pack, identical in every member: `xworld-europe` |
| `member`     | yes      | identifier of this folder inside the pack, unique: `europe-vfr` |
| `pack_name`  | no       | display name of the pack: `simHeaven X-World Europe`. One member is enough, all is better |

Rules for identifiers: ASCII letters, digits, `-`, `_`, `.`; no spaces; choose
them once and never change them. They are what makes a folder recognisable
after a user renamed or moved it. The display names (`name`, `pack_name`) can
change freely.

Example, folder "simHeaven_X-World_Europe-1-vfr":

```
name|X-World Europe - 1 VFR
module|https://cdn.example.com/updater/xworld/europe-vfr
version|4.3.0
locked|false
zone|custom
pack|xworld-europe
member|europe-vfr
pack_name|simHeaven X-World Europe
```

Put these keys in the `skunkcrafts_updater_config.txt` you publish AND in the
cfg shipped inside your download packages. A folder installed before you added
the keys adopts them at its next check: the client copies `pack`, `member`,
`pack_name` and `pack_members` from the depot cfg into the local one, exactly
as it already follows a changed `name` or `module`. So existing users are
grouped into the pack as soon as your depots carry the keys, without
reinstalling.

## 4. Declaring the members: `pack_members`

One member, the "main" one, lists every member of the pack with a default
folder and the URL of its depot:

```
pack_members|<member>=<default folder>=<depot URL>;<member>=<default folder>=<depot URL>;...
```

- `<member>` is the member identifier (the `member` key of that folder).
- `<default folder>` is a path relative to the X-Plane folder, with `/`
  separators: `Custom Scenery/simHeaven_X-World_Europe-1-vfr`. It is used only
  when that member is not installed anywhere (see below). It may contain
  spaces. It must stay inside the X-Plane folder (`..` and absolute paths are
  rejected) and outside X-Plane's own folders (`Resources/default *`,
  `Global Scenery`, `Custom Data`, `Output`, ...): a member is a folder inside
  `Custom Scenery`, `Aircraft`, `Resources/plugins` or a folder of your own at
  the X-Plane root.
- `<depot URL>` is the full URL of that member's depot (what `module` holds in
  its cfg, zone `custom`).
- Entries are separated by `;`, on one line. A malformed or unsafe entry is
  ignored and logged; the others still apply.

Example, on the main member only:

```
pack_members|europe-library=Custom Scenery/simHeaven_X-World_Europe-0-library=https://cdn.example.com/updater/xworld/europe-library;europe-vfr=Custom Scenery/simHeaven_X-World_Europe-1-vfr=https://cdn.example.com/updater/xworld/europe-vfr;europe-regions=Custom Scenery/simHeaven_X-World_Europe-2-regions=https://cdn.example.com/updater/xworld/europe-regions
```

Every member may carry the line; the client merges them. Keeping it on one
member only is simpler to maintain.

## 5. What the client does

**Grouping.** After scanning the X-Plane folder, every folder whose cfg has
the same `pack` value becomes one entry named after `pack_name` (or the pack
identifier if no member gives a name). The version shown is the members'
common version, or "mixed" when they differ. The status is the worst of the
members'. "Check" and "Update" act on every member; the pack view shows each
member with its own status, and opens it for details.

**Missing members.** When a member listed in `pack_members` is not installed
anywhere, the pack view says so and offers to create it. On confirmation the
client creates the default folder with a minimal cfg pointing at the depot
URL, then the update fills it. This is how a pack gains a new folder in a
later version: add the member to `pack_members` in the main member's depot
cfg, publish its depot, done. Users get the new folder at their next update,
after confirming the creation. The client never creates folders silently.

**Renamed or moved members.** Members are found by scanning, not by path, so a
renamed folder stays a member. `pack_members` default folders only matter for
members that are absent.

**Removed members.** The client never deletes a folder. To retire a member,
stop listing it in `pack_members`; installed copies remain and keep updating
from their own depot as long as it exists. Publishing an empty depot for it
(no files in the whitelist, a blacklist of what to delete) empties the folder.

**Versions.** Give every member the same `version` string when you release,
even when a member's files did not change: the pack then shows one version
instead of "mixed", and users understand what they have.

## 6. Changelog

Each member may publish a `changelog.txt` next to its cfg (the same file you
ship in the folder): the newest version first as a bare line, e.g. `4.3.0`,
entries below it starting with `- `, a blank line between versions. The
client shows it in the member's details and highlights the versions newer
than the installed one. For a pack, the simplest is one changelog, on the
main member, covering the whole product.

## 7. Checklist for a first publication

1. Choose the pack identifier and one member identifier per folder. They are
   permanent.
2. Add `pack`, `member`, `pack_name` to every member's cfg; add
   `pack_members` to the main member's cfg.
3. Ship the same cfg files inside your download packages.
4. Publish each member as a depot (`xp_depot <zone> <folder> update`, or your
   own tool). One depot per folder, any URLs.
5. Test with a fresh X-Plane folder: install the main member only, run the
   updater, accept the creation of the missing members, let it download.
6. Test with a renamed member folder: it must still appear inside the pack.

## 8. Reference

```
pack|<pack id>                                   every member
member|<member id>                               every member
pack_name|<display name>                         any member (all is better)
pack_members|<id>=<X-Plane relative folder>=<depot URL>;...   main member
```

All other keys, files and rules are those of an ordinary depot.
