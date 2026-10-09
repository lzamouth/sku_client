# Publishing with SkunkCrafts Updater

Guide for developers: how to build an update depot, how to run beta depots
next to the production one, and how to group several folders into a pack.
Applies to the 4.x client and to the 3.x clients still in use; the few
4.x-only features are marked as such.

## Part 1 - A depot

### 1.1 How it works

Your users install your product once, from your download package. From then
on the updater keeps the installed folder identical to a copy you publish on
a web server, the **depot**. At every check the client downloads a few small
text files from the depot, compares them with what the user has, and fetches
only the files that are new or modified. It never contacts anything but the
URL written in the product's configuration file.

A depot is a plain folder served over HTTP or HTTPS. Any static hosting works
(CDN, web space, object storage): no script runs on the server.

### 1.2 The configuration file

At the root of your product folder lives `skunkcrafts_updater.cfg`, one
`key|value` per line. The clients find it by scanning the X-Plane folder, as
3.x always did:

- under `Aircraft`, down to ten levels deep; the search stops in a folder
  that holds a `.acf` without a configuration (an aircraft not managed);
- in a folder directly inside `Custom Scenery` or `Resources/plugins`, and
  not deeper: `Custom Scenery/<your scenery>/skunkcrafts_updater.cfg` is
  found, `Custom Scenery/<your scenery>/<sub-folder>/...` is not, and
  neither is a plugin a user parked in `Resources/plugins/(disabled)/`;
- in a folder directly at the X-Plane root (4.x).

A product installed anywhere else is only managed if the user adds its
folder by hand ("Add folder..." in the 4.x Options).

The file itself:

```
name|Aerobask Dassault Falcon 8X
module|https://cdn.example.com/updater/aerobask/F8X_XP12
version|12.0b3
locked|false
zone|custom
```

| Key       | Meaning |
|-----------|---------|
| `name`    | what the user sees in the list |
| `module`  | the URL of the depot, the folder that holds the files below |
| `version` | the version installed (locally) or published (on the server). Any string; the client only compares for equality |
| `locked`  | `true` while you upload: clients then refuse to download from the depot. Optional, absent means unlocked |
| `zone`    | keep `custom`: it means `module` is a full URL. Other values are historical SkunkCrafts aliases |

Ship this file inside your download package, so that a fresh install is
recognised by the updater. Both the 3.x and the 4.x clients leave it in
place and read the same keys; the 4.1 client keeps its beta channels apart, in its
own sub-folder (Part 3).

This same file is uploaded to the depot as `skunkcrafts_updater.cfg`; there
it is the **remote configuration**, the first thing a client reads. Its
`version` is what the client compares with the local one, its `locked` flag
stops downloads, and its `name`, `zone` and `module` are copied into the
user's local file when they differ. That last point matters, see 1.6.

### 1.3 What the depot holds

```
skunkcrafts_updater.cfg                the configuration above (uploaded last)
skunkcrafts_updater_whitelist.txt      one line per file:  path|crc32
skunkcrafts_updater_sizeslist.txt      one line per file:  path|size in bytes
skunkcrafts_updater_blacklist.txt      one path per line:  files to DELETE on the user's side
changelog.txt                          optional, see 1.5
... every file of the product, at the same relative paths
```

- Paths are relative to the product folder, with `/` as separator whatever
  your operating system, and case matters: Linux and macOS users have
  case-sensitive disks, so the path in the list must match the file exactly.
- The CRC32 is the standard one (as `crc32` in zlib, or the `crc32` shell
  command), written as an unsigned decimal number.
- `-1` instead of a CRC means "install this file if it is missing, then never
  touch it again": preferences, profiles, anything the user is meant to edit.
- The blacklist is the only way to remove a file from users' installs. Use it
  when you delete or rename a file; otherwise the old one stays forever.
  Entries can be pruned after a few versions.
- Files and folders whose name starts with a dot are not published.

The whitelist and the sizeslist are generated for you by `xp_depot`
(section 1.4). If you publish by hand, generate them with any script; the
format is deliberately trivial.

### 1.4 Publishing with xp_depot

`xp_depot` is a stand-alone Python script (3.8 or later, no dependency). It
takes a local folder that is the exact copy of what users should end up with,
computes the CRCs, compares with what the server already has, uploads only
what changed, then the lists, then the configuration. It handles the lock for
you: the depot is locked before the first upload and unlocked by the final
configuration upload, so an interrupted upload leaves the depot locked rather
than half updated.

In the local folder, next to the product files:

```
skunkcrafts_updater_config.txt       your configuration; uploaded as skunkcrafts_updater.cfg
skunkcrafts_updater_ignorelist.txt   paths never uploaded (your working files)
skunkcrafts_updater_oncelist.txt     paths sent once, never updated (CRC -1)
skunkcrafts_updater_blacklist.txt    paths to delete on the user's side
```

Typical session:

```
xp_depot <zone> <product folder> check     # lists what would be uploaded
xp_depot <zone> <product folder> update    # uploads, with the lock
xp_depot <zone> <product folder> status    # locked or not
```

If you upload by hand instead: set `locked|true` in the remote configuration
first, upload the files, then the lists, and finish with the configuration
carrying `locked|false`. Never the other way round.

### 1.5 Versions and changelog

Bump `version` at every release, in the configuration you ship and in the
one you upload; it is the only thing that tells the client an update is
worth showing as "new version" rather than "repair".

`changelog.txt`, published like any other file, is displayed by the 4.0
client with the sections newer than the installed version highlighted. Its
format: the newest version first, as a bare line, entries starting with
`- `, a blank line between versions:

```
12.0b3
- HSI: fixed FMS bearing pointer always parked
- 3D: new red pillow for the Aerobask livery

12.0b2
- FMS: fixed crash when summoning the menu on a runway
```

The first line is the version: `xp_ship`, and some aircraft themselves, read
it from there.

### 1.6 The classic mistake: the beta URL in the configuration

The configuration you upload is copied into your users' local file: when the
remote `module` differs from the local one, the client adopts the remote one.
This is what lets you move a depot to another server without asking anyone
to reinstall.

It is also why **the configuration must always point at the production
depot**, whatever depot you upload it to. If you put the beta URL in
`skunkcrafts_updater_config.txt` to publish a beta, every client that reads
that configuration, from the beta depot or, worse, from the production depot
you updated with the same file, silently switches to the beta URL and stays
there. Half of the support tickets around betas come from this.

The beta URL lives in one place only: the tester's beta file, described in
Part 2. The configuration file never changes between production and beta
uploads, except for `version`.

### 1.7 Checklist

1. `skunkcrafts_updater_config.txt` with `name`, `module` (production URL,
   full, `https://`), `version`, `locked|false`, `zone|custom`.
2. The same file, named `skunkcrafts_updater.cfg`, inside your download
   package.
3. Working files listed in the ignorelist, user-editable files in the
   oncelist.
4. `xp_depot ... check`, then `update`.
5. Test on a clean X-Plane folder: install the package, run the updater, it
   must say "up to date"; change one file, it must say "repair".

## Part 2 - Beta depots

### 2.1 Principle

A beta is a second depot, at another URL, holding the beta build. It is
published exactly like the production one, with the **same configuration
file** (production URL, see 1.6), only `version` differs (`12.0b3` against
`12.0r2`, for example). Nothing on the production depot changes.

Testers opt in with a small file dropped into the product folder. Production
users never see the beta.

### 2.2 The tester's file

`skunkcrafts_updater_beta.cfg`, next to `skunkcrafts_updater.cfg`:

```
module|https://cdn.example.com/updater/aerobask/F8XBeta
zone|custom
enabled|true
```

| Key       | Meaning |
|-----------|---------|
| `module`  | the URL of the beta depot. Required |
| `zone`    | `custom` |
| `enabled` | `true` to follow the beta, `false` to go back to production. Optional, default `true` |

Give this file to your testers (a zip with the file inside, or the three
lines to paste). When it is present and enabled, the client checks and
updates from the beta depot; the user's main configuration keeps the
production URL and is left alone.

The 3.x client shows a "beta" toggle for a folder that has this file;
removing the file, or setting `enabled|false`, puts the tester back on
production: the next check compares with the production depot and "repairs"
the folder to the production build.

The 4.x client absorbs the file into its own list of channels as soon as it
loads the folder: the file disappears, and a channel drop-down (`Main` /
`beta`) appears in the addon list. Testers who put the file back get the
same channel, not a second one. They go back to production by picking
`Main`, or remove the channel altogether ("Remove channel..." in the
add-on's details); a new file with `enabled|false` makes them follow `Main`
too, the channel staying available.

### 2.3 Several betas at once (4.x client)

The 4.x client supports any number of channels. Each additional file
`skunkcrafts_updater_beta_<channel>.cfg` declares one: for example
`skunkcrafts_updater_beta_rc.cfg` declares the channel `rc`,
`skunkcrafts_updater_beta_experimental.cfg` the channel `experimental`. Same
three keys. The drop-down then lists `Main`, `beta`, `rc`, ... and the
user picks. The plain `skunkcrafts_updater_beta.cfg` remains the channel
named `beta`.

The 3.x client only knows `skunkcrafts_updater_beta.cfg`; testers on 3.x see
that one channel only.

### 2.4 Naming a channel (4.x client)

The channel is named after the tester's file (`beta`, or `<x>` for
`skunkcrafts_updater_beta_<x>.cfg`), or after the `channel|` line inside it
when there is one:

```
module|https://cdn.example.com/updater/aerobask/F8XBeta
zone|custom
channel|Private beta
```

What a client does with a tester's file, in this order:

1. A file pointing at your production depot adds nothing.
2. A channel already following the same depot (compared on the URL the
   client really uses: zone, trailing slashes) stays the same channel: it
   is renamed only if the file carries a `channel|` line with another name.
   Testers who put the same file back after seeing it disappear (it is
   absorbed) therefore never get a second channel.
3. Otherwise, as in 3.x where one beta file means one beta, the file moves
   to its new URL the channel of the same name, or the channel it would have
   been named after without the `channel|` line (`beta` for that file name,
   `<x>` for `skunkcrafts_updater_beta_<x>.cfg`), renaming it.
4. Otherwise it adds a channel.

To rename a channel your testers already have, hand them a new
`skunkcrafts_updater_beta.cfg` with the new name and the current URL. The
file is absorbed as soon as the client loads the folder. Nothing goes through the depot: the configuration uploaded there is
the production one, identical on every channel, and a channel only ever
substitutes its own URL for the production one. Clients on the production channel
ignore the key, so if you run a single beta depot the line can simply live in
your shared configuration file. With several beta depots, each needs its own
name, hence its own copy of the configuration. The 3.x client ignores the key
and keeps calling it "beta".

### 2.5 Going to production

When the beta becomes the release, publish the build to the production depot
with the release version. Testers who stay on the beta channel keep
following the beta depot: either publish the release there too, or tell them
to switch back to Main. Do not delete a beta depot while testers are on it:
an unreachable depot shows as an error in their list.

### 2.6 Public channels (4.2 client)

A beta open to everyone needs no file to hand out: declare it in the
configuration of your production depot, one line per channel,
`channel.<name>|<URL of its depot>`:

```
name|Aerobask Dassault Falcon 8X
module|https://cdn.example.com/updater/aerobask/F8X_XP12
version|12.0b3
locked|false
zone|custom
channel.Public beta|https://cdn.example.com/updater/aerobask/F8XBeta
channel.Release candidate|https://cdn.example.com/updater/aerobask/F8XRC
```

- The name may contain spaces (spaces at both ends are ignored). It cannot
  be `Main` nor `stable`, and must not end with `.module` or `.zone`.
- The URL is a full one (zone `custom`).
- The 3.x client ignores these lines; they follow its rules (one value per
  key, no `|` in a value), so keep one line per channel.

What the 4.2 client does with them, at every check:

- It adds each channel to the user's drop-down, **without following it**:
  the user stays on `Main` until picking the channel.
- Renaming a line (same URL) renames the channel; changing the URL of a
  name moves the channel, for those who follow it too.
- A user who removes the channel ("Remove channel...") does not get it back.
- When the line disappears from the production configuration, the channel
  disappears for users who do not follow it; those who follow it keep it
  until they go back to `Main`. Publish the release on the beta depot too,
  or keep the line until your testers have moved back.
- A tester who already follows the same depot through a tester file keeps
  that private channel, under its own name.

Private betas stay with tester files (2.2): a channel declared in the
configuration is visible to every user of the product. The configuration
is the same on every channel's depot; the client reads the public channels
from whichever depot it checks, but only the production depot's
configuration can withdraw one.

## Part 3 - On the user's side: local files and the 4.0 migration

### 3.1 What the client writes

Inside the product folder, both the 3.x and the 4.x clients keep the same
files, at the same names:

```
skunkcrafts_updater.cfg              the configuration (yours, then maintained by the client)
skunkcrafts_updater_ignore.txt       the user's own list of paths the updater must never touch
skunkcrafts_updater_beta.cfg         the tester's beta file, until the 4.x client absorbs it
```

This matters beyond the updater: aircraft read `skunkcrafts_updater.cfg` to
show their version string, and publishing tools read it too. The client
therefore never moves or renames it, and from 4.1 writes nothing of its own
in it: it holds the keys of the 3.x format only, and `version` is always the
installed version. That is the file, and the key, your aircraft may rely on.

The client's own sub-folder `skunkcrafts/` holds:

```
skunkcrafts/crc_cache.txt            the cache that makes checks fast (rebuilt if deleted)
skunkcrafts/channels/<name>.cfg      one file per beta channel: channel|, module|, zone|
skunkcrafts/channel.cfg              the channel followed: channel|<name> (stable = Main)
```

The beta file is absorbed into `skunkcrafts/channels/` and removed the first
time the client loads the folder, so a folder managed by 4.x shows no beta
file any more; the 3.x client then sees only the production channel there.
Clients 4.0.0 to 4.0.2 kept the channels as extra `channel...` lines in
`skunkcrafts_updater.cfg`; 4.1 moves them to the sub-folder on first load.
Early 4.0 betas (up to beta9) had moved the whole configuration into
`skunkcrafts/updater.cfg`; such folders are migrated back on first load too.
A user removes a channel from the add-on's details (the client asks for a
confirmation and keeps the file aside as `<name>.cfg.removed`), or with
`sku remove-channel`.

The `skunkcrafts/` sub-folder is the client's own. A depot cannot publish
into it nor delete from it (such lines are ignored), and `xp_depot` never
uploads it. If you run the updater on your own working copy, the folder
appears there too; leave it out of anything you package by hand.

Consequences for you:

- Keep shipping `skunkcrafts_updater.cfg` in your package: it is what makes
  a fresh install recognised, by both clients.
- When a user reports a problem, the configuration to ask for is
  `skunkcrafts_updater.cfg`, and the log is
  `Output/skunkcrafts/updater/updater_log.txt` in the X-Plane folder (4.x) or
  `skunkcrafts_updater_log.txt` at the X-Plane root (3.x).
- The configuration file is never part of the whitelist, so an update never
  overwrites the user's settings; only `name`, `zone`, `module` and
  `version` are rewritten by the client, from your remote configuration.

### 3.2 What the client never does

It never deletes a file that is not in the blacklist: files the user added
(liveries, custom profiles) survive updates. It never writes outside the
product folder and `Output/skunkcrafts/updater/`. It never downloads while
the depot is locked. It never creates a folder without asking (packs, 5.5).

## Part 4 - Pitfalls, in order of how often they bite

1. **The beta URL in the configuration** (1.6). The remote `module` is
   copied to every user. Production URL always; the beta URL only in the
   tester's beta file. And never ship a `skunkcrafts_updater_beta.cfg` in a
   package or a depot for 3.x users: every one of them would be a tester.
   The 4.x client (from 4.1.2) reads a tester file listed in a depot as an
   offer: the channel appears in the user's list, but is only followed if the
   user picks it, and does not come back once removed. It never takes
   `skunkcrafts_updater.cfg` or `skunkcrafts_updater_ignore.txt` from a
   depot, nor deletes them through a blacklist: they belong to the client and
   to the user.
2. **Files the product modifies at run time**: preferences, persisted state,
   logs written inside the product folder. If they are in the whitelist with
   a real CRC, every check says "repair" and every update erases the user's
   settings. Put them in the oncelist (CRC `-1`) or leave them out of the
   depot entirely.
3. **Stale CDN cache**. After an upload, the lists and the configuration
   must be fresh, or clients compare new files against old lists and loop on
   "repair". `xp_depot` purges the Bunny cache itself; on another CDN, purge
   or keep the TTL of the `.cfg` and `.txt` files short. The 4.x client helps:
   it requests the configuration with a unique query string (`?t=...`) and
   the lists, changelog and readme with the published version (`?v=...`), so
   a CDN that keys its cache on the full URL always serves the configuration
   fresh and never pairs a new version with old lists. A CDN configured to
   ignore query strings still needs a purge. A server that refuses URLs with
   a query string (404 or 403) is simply asked again without, from 4.1.1:
   it works, but loses that protection. The lists are also requested
   conditionally (`ETag` / `Last-Modified`), which a static host answers
   for free.
4. **Case and spelling of paths**. `Objects/Wing.obj` and `objects/wing.obj`
   are two different files on Linux and macOS. Generate the lists from the
   real files (xp_depot does), never type them.
5. **Uploading in the wrong order by hand**: configuration first, files
   after. Users who check in between download a mix. Lock, files, lists,
   configuration with `locked|false`. An interrupted upload leaves the depot
   locked: run `xp_depot ... unlock` or simply upload again.
6. **A depot that serves a web page**. A wrong URL, a hotlink protection or
   a login page answer HTML with status 200. The client refuses it ("server
   returned a web page instead of ...") and shows an error; check the URL in
   a browser, it must display the raw configuration text. The certificate
   must be valid for HTTPS: the client trusts the system store on Windows,
   the Mozilla bundle elsewhere. The clients present themselves as a web
   browser (the same User-Agent as 3.x, from 4.1.1 in 4.x too), so a rule
   that serves files to browsers only does not lock them out; any other
   filter on the User-Agent will. On Windows, revocation is checked when the
   revocation server can be reached (since 4.0.1, a network that blocks it no
   longer makes every connection fail).
7. **Changing `module` in the remote configuration** moves every user at
   their next check, one way. A typo there breaks everyone until you fix it
   at the old URL. Test with `xp_depot ... check` and a copy of the product
   before publishing a moved depot.
8. **Version strings**: compared as text, so `12.0` and `12.0.0` differ, and
   so do `12.0r1` and `12.0R1`. Keep one convention. `xp_ship` copies the
   changelog's first line into the configuration, which keeps both aligned.
9. **Text file hygiene**: `|` cannot appear in a path or a name (it is the
   separator); no byte order mark; LF or CRLF both work but keep one; no
   trailing spaces in paths.
10. **Exotic file names**: the client URL-encodes paths, so spaces and
    accents work when the server decodes them, which static hosts do. Very
    long paths hit the Windows limit at 260 characters unless long paths are
    enabled: keep folder trees shallow.
11. **Removing a file**: forgetting the blacklist leaves it on every disk
    forever. Renaming a file is a removal plus an addition.
12. **Zones**: any value other than `custom` is a historical alias for
    SkunkCrafts' own hosting and means nothing to anyone else. Use
    `zone|custom` with the full URL in `module`.

## Part 5 - Packs: a product made of several folders

SkunkCrafts Updater 4.x - guide for developers whose product is installed as
several folders (for example a scenery library plus several regional scenery
folders). Applies to any developer whose product installs as several folders.

### 5.1 What a pack is

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

### 5.2 One depot per member

Publish each folder as a normal depot, with `xp_depot` or by hand, at any URL,
in any zone. Per member folder, on the server:

```
skunkcrafts_updater.cfg               (uploaded from skunkcrafts_updater_config.txt)
skunkcrafts_updater_whitelist.txt     path|crc32        (generated)
skunkcrafts_updater_sizeslist.txt     path|bytes        (generated)
skunkcrafts_updater_blacklist.txt     files to delete   (optional)
changelog.txt                         optional, see 5.6
... the files of that folder
```

The file lists and the lock work per member. Uploading member A does not lock
member B; a user who checks for updates while you upload A sees A locked and B
up to date, which is harmless.

### 5.3 The cfg of a member

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

### 5.4 Declaring the members: `pack_members`

One member, the "main" one, lists every member of the pack with a default
folder and the URL of its depot:

```
pack_members|<member>=<default folder>=<depot URL>;<member>=<default folder>=<depot URL>;...
```

- `<member>` is the member identifier (the `member` key of that folder).
- `<default folder>` is a path relative to the X-Plane folder, with `/`
  separators: `Custom Scenery/simHeaven_X-World_Europe-1-vfr`. It is used only
  when that member is not installed anywhere (see 5.5). It may contain
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

### 5.5 What the client does

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

### 5.6 Changelog

Each member may publish a `changelog.txt` next to its cfg (the same file you
ship in the folder): the newest version first as a bare line, e.g. `4.3.0`,
entries below it starting with `- `, a blank line between versions. The
client shows it in the member's details and highlights the versions newer
than the installed one. For a pack, the simplest is one changelog, on the
main member, covering the whole product.

### 5.7 Checklist for a first publication

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

### 5.8 Reference

```
pack|<pack id>                                   every member
member|<member id>                               every member
pack_name|<display name>                         any member (all is better)
pack_members|<id>=<X-Plane relative folder>=<depot URL>;...   main member
```

All other keys, files and rules are those of an ordinary depot.

