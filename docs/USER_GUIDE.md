# SkunkCrafts Updater 4.0 - User guide

SkunkCrafts Updater keeps your X-Plane 11/12 add-ons (aircraft, sceneries,
plugins) up to date against their developers' update servers. It checks what
you have, downloads only what changed, and can repair a damaged install.

## 1. Which download

One program, five downloads. Pick the one for your system and the way you
like to install things:

| System  | Download                              | What it is |
|---------|---------------------------------------|------------|
| Windows | `SkunkcraftsUpdater-<version>-windows.zip`       | the program alone, `SkunkcraftsUpdater.exe` |
| Windows | `SkunkcraftsUpdater-<version>-windows-setup.exe` | installer: Start menu entry, uninstaller |
| macOS   | `SkunkcraftsUpdater-<version>-macos.zip`         | the application bundle, Intel and Apple Silicon |
| Linux   | `SkunkcraftsUpdater-<version>-linux.zip`         | the program alone, `SkunkcraftsUpdater_lin` |
| Linux   | `SkunkcraftsUpdater-<version>-x86_64.AppImage`   | single file that integrates with the desktop menu |

The program alone works from two places:

- **Inside your X-Plane folder**, the classic way: drop it next to
  `X-Plane.exe` (or the X-Plane application) and it manages that
  installation. One copy per X-Plane installation.
- **Anywhere else**: it finds the X-Plane installations X-Plane itself
  declared, lets you pick one, and remembers your choice. The installer, the
  macOS bundle in Applications and the AppImage work this way.

## 2. Installing

**Windows.** Unzip, or run the installer. Windows SmartScreen may warn that
the program is not commonly downloaded: choose "More info", then "Run
anyway". The program is not signed with a commercial certificate.

**macOS.** Unzip and move `SkunkcraftsUpdater.app` where you like. The
application is not notarized: the first time, right-click it and choose
"Open", or allow it under System Settings > Privacy & Security. If macOS
says the file is damaged, it was downloaded by a browser that quarantined
it; run `xattr -d com.apple.quarantine` on it in a Terminal, or use the
right-click "Open" route.

**Linux.** Unzip and make the program executable (`chmod +x`), or make the
AppImage executable and run it. Nothing else is needed: the program only
depends on the system C library (glibc 2.35 or newer, i.e. any distribution
from 2022 onwards). Under a Wayland desktop the program runs through
XWayland by default, which gives smooth resizing and the desktop's own title
bar; on a display set to a fractional scale (125 %, 150 %) that can look
blurry, in which case pick "Wayland" under Options > Window system.
AppImageLauncher 2.2 (the old stable release) cannot start AppImages built
with the current runtime; use version 3.0 or the plain program from the zip.

## 3. First start

If the program sits in an X-Plane folder, it opens that installation. Else
it uses the one you picked last time, then the installations declared in
X-Plane's own list. When it cannot decide, it shows the installation chooser:
pick one, or "Browse..." to any X-Plane folder. You can change it later in
Options.

The Addons page then lists every add-on it found: it scans `Aircraft`,
`Custom Scenery`, `Resources/plugins` and the X-Plane root for folders that
carry a SkunkCrafts Updater configuration. Add-ons that do not use
SkunkCrafts Updater are simply not listed.

## 4. The Addons page

**Filters** show or hide aircraft, sceneries, plugins and the rest.

**Columns**: Channel, Name, Version, Status. Drag a boundary in the title
row to resize the columns; double-click it to reset.

- **Channel** shows a drop-down only when the add-on offers more than the
  main channel, for example a beta (see section 7). Main is blue, the others
  yellow.
- **Version** is the version installed on your disk.
- **Status** is what the last check found:

| Status | Meaning |
|--------|---------|
| Information from local configuration | not checked yet |
| Checking / Updating ... | in progress, with a file counter |
| All files are up to date | nothing to do |
| Version X available: N files, S to download | a new version is published |
| N files missing or changed, S to download | same version, but your copy differs: repair it |
| Files up to date but local and remote versions differ | harmless, an update/repair fixes the version number |
| Update repository locked for maintenance | the developer is uploading, try later |
| Updates disabled by user | you turned online updates off for this add-on |
| Cannot retrieve ... / server returned a web page | network or server problem, see section 11 |

**Buttons**: "Check for new addons" rescans the X-Plane folder; "Reload all
configs" re-reads the configuration files; "Check all updates" contacts the
servers and compares; "Update / repair all" downloads what is needed. The
update button lights up when at least one visible add-on needs it. While
work runs, "Cancel all" stops it; files already downloaded are kept.

Click a row to open the add-on's details. A row marked "pack" is a product
made of several folders (section 6).

## 5. Add-on details

The top shows name, versions, folder, source and status, then:

- **Enable online updates**: unchecked, the add-on is skipped by every
  check and update.
- **Enable liveries updates** (aircraft): unchecked, the `liveries` folder
  is left alone, useful if you removed liveries you do not want.
- **Channel**: the main channel or a beta the developer gave you access to.

The buttons act on this add-on only: "Check updates", "Verify integrity"
(re-reads every file instead of trusting the cache, use it when something
looks corrupted), "Update / repair", and "Reload config".

Four tabs:

- **Files**: what the last check decided, one line per file with its
  reason: `new` (missing on your disk), `changed`, `delete` (removed by the
  developer), `ignored` (listed in your ignore file, section 10), `error`.
  After an update, done lines turn green.
- **Report**: the text of the last check or update, the same as the file
  saved for support.
- **Changelog**: what changed, version by version; sections newer than your
  version are marked with a triangle and coloured, the installed one is
  marked "(installed)".
- **Readme**: the developer's notes, when published.

## 6. Packs

Some products install as several folders (a scenery library and its
regions, for example). They appear as one "pack" row with a common version
and status. Its page lists the members; "Check all members" and "Update /
repair all" act on all of them, and each member opens its own details.

When the developer adds a folder to the pack, the page says that a declared
member is missing and offers "Create the missing folders...". Nothing is
created without your confirmation; the following update fills the new folder.

## 7. Channels and betas

Every add-on follows its **Main** channel, the developer's release. To test
a beta, the developer hands you a small file, `skunkcrafts_updater_beta.cfg`,
to drop into the add-on's folder. At the next start the updater absorbs it
(the file disappears, its settings are kept) and the Channel drop-down
appears, with the beta in yellow. Pick the beta and update to get the beta
build; pick Main and update to go back to the release. A developer may offer
several channels; each comes with its own file.

Putting the same file back does no harm: a channel already following that
beta is updated, never added twice. To get rid of a beta, pick it, press
"Remove channel..." next to the drop-down and confirm with "Remove": the
add-on goes back to Main. Only the developer's file can bring that channel
back.

## 8. Options

- **Automatic action on startup**: nothing, check everything, or check and
  update everything.
- **Theme**: dark, light, or follow the system. **Bevelled widgets** gives a
  classic relief look instead of the flat one.
- **Window system** (Linux): automatic, Wayland or X11, applied at the next
  start. See section 2.
- **Verbose logging**: only when support asks for it, the log gets large.
- **Max concurrent downloads** and **CRC checks**: leave the defaults
  unless a slow disk or connection suffers.
- **Text size**: the whole interface scales with it.
- **X-Plane installation**: the one being managed; "Change..." opens the
  chooser.
- **Custom addon folders**: an add-on installed outside the scanned folders
  can be added by hand; it then appears under the "Other" filter.
- **Log file**: where it is, with a button to open the folder.
- **Updater updates**: whether the program looks for a newer version of
  itself at start; "Check now" asks immediately. See section 9.

## 9. Keeping the updater itself up to date

At every start (at most once an hour) the program checks whether a newer
version of itself is published. When one exists, a banner appears above
every page with three buttons: "What's new" shows the changes, "Later" hides
the banner until the next version, and the third one installs it:

| How you run it | Button | What happens |
|---|---|---|
| Linux, AppImage | Update | the new version replaces the running file in place; menu entry and dock icon stay valid |
| Linux, program alone | Update | the new program replaces the running one, in your X-Plane folder or wherever it is |
| Windows, program alone | Update | same; the previous program stays a moment as `SkunkcraftsUpdater.exe.old` and is removed at the next start |
| Windows, installed | Update, then Install now | the new setup is downloaded; the updater quits and the setup runs (it asks for administrator rights, like the first time) and can start the new version when it finishes |
| macOS | Update | the new application bundle replaces the running one, in Applications or in your X-Plane folder; the previous one stays a moment as `SkunkcraftsUpdater.app.old` and is removed at the next start |

Every download is verified before it is used. Where the program is replaced
in place, the banner then offers "Restart now"; until you restart, the old
version keeps running. Nothing changes in your add-ons or settings.

If the replacement fails (a folder you cannot write to, for example), the
banner says so and the downloaded package stays in a `downloads` folder next
to the updater's preferences, with a button to open it, so you can install it
by hand.

## 10. What the updater writes, and how to keep files out of updates

Inside each managed add-on folder, the updater keeps the same files as the
previous versions did: `skunkcrafts_updater.cfg` (the add-on's settings,
which the aircraft itself may also read) and `skunkcrafts_updater_ignore.txt`.
A beta file you dropped there is absorbed and removed. The updater's own
`skunkcrafts` sub-folder holds the cache that makes checks fast
(`crc_cache.txt`, which can be deleted at any time: the next check rebuilds
it) and your beta channels (`channels/` and `channel.cfg`): deleting those
loses the channels, so leave them alone.

To keep a file the way you changed it, list its path, relative to the
add-on folder, one per line, in `skunkcrafts_updater_ignore.txt`; the
updater will neither check nor overwrite it. Files you added yourself are
never touched: the updater only deletes what the developer explicitly
removed.

In the X-Plane folder, `Output/skunkcrafts/updater/` holds the log
(`updater_log.txt`), the last report of each add-on, and copies of the
servers' lists. Your own preferences live in your user profile: `AppData`
on Windows, `Library/Application Support` on macOS, `~/.config` on Linux.

## 11. Troubleshooting

- **"X is not an X-Plane installation"**: the folder must contain
  `Aircraft`, `Custom Scenery`, `Resources/plugins` and `Output`. Point the
  chooser at the folder that holds the X-Plane program.
- **"Cannot retrieve the remote configuration"**: no network, a firewall, or
  the developer's server is down. Try later; the message gives the reason.
- **"server returned a web page instead of ..."**: the add-on's server
  answered an error page. Usually a problem on the developer's side.
- **"Update repository locked for maintenance"**: the developer is uploading
  a new version. Wait a little and check again.
- **"N files could not be read"**: a file on your disk is locked or
  unreadable, often because X-Plane is running. Close X-Plane and retry.
- **Every check says "repair" for the same add-on**: a file the add-on
  rewrites while running, or one you changed on purpose. Put it in
  `skunkcrafts_updater_ignore.txt` (section 10) or let the developer know.
- **An update stopped half way**: run "Update / repair" again; what was
  downloaded is kept, only the rest is fetched.
- **Reporting a problem**: send the developer the add-on's report (Report
  tab) and the log file (Options > Log file > Open folder).

## 12. Command line

`sku` (`sku.exe` on Windows) is the same updater without a window, for
scripts and support:

```
sku instances                    X-Plane installations found
sku --xp DIR list                add-ons of an installation
sku --xp DIR check [name]        check for updates
sku --xp DIR update [name]       update / repair
sku --xp DIR report [name]       last report
sku --xp DIR changelog [name]    what changed
sku --xp DIR channels [name]     beta channels, the followed one marked *
sku --xp DIR remove-channel CHANNEL name   remove one add-on's beta channel
sku client                       is a newer updater published?
```

Add `--dry-run` to a check to write nothing at all, `--full` to ignore the
CRC cache, `--verbose` for the debug log.
