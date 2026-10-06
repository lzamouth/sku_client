SkunkCrafts Updater @VERSION@
==============================

Keeps your X-Plane 11/12 add-ons (aircraft, sceneries, plugins) up to date
against their developers' update servers.

Which download
--------------

One program, five downloads; pick the one matching your system and the way
you like to install things:

     Windows   SkunkcraftsUpdater-<version>-windows.zip        the program alone
               SkunkcraftsUpdater-<version>-windows-setup.exe  installer (Start menu, uninstaller)
     macOS     SkunkcraftsUpdater-<version>-macos.zip          the application bundle
     Linux     SkunkcraftsUpdater-<version>-linux.zip          the program alone
               SkunkcraftsUpdater-<version>-x86_64.AppImage    single file, desktop menu integration

The program alone can be dropped into your main X-Plane folder (the classic
way: it then manages that installation) or anywhere else: it finds the
X-Plane installations declared by X-Plane itself and lets you pick one, or
browse to any X-Plane folder. The installer, the macOS bundle in Applications
and the AppImage work the same way from outside X-Plane.

macOS: the application is not notarized. If macOS refuses to open it,
right-click the application and choose "Open", or allow it under
System Settings > Privacy & Security.

Linux: an AppImage needs no installation; make it executable and run it.
Under a Wayland desktop the program uses XWayland by default (smooth
resizing, the desktop's own title bar); on a fractional-scale display pick
"Wayland" in Options > Window system if it looks blurry.
The program only depends on the system C library (glibc 2.35 or newer, i.e.
Ubuntu 22.04 or any distribution from 2022 onwards).
If you use AppImageLauncher, you need version 3.0 or newer: the old 2.2
release (2020) can add the AppImage to your menu but cannot start it, like
every AppImage built with the current runtime. In that case simply use
SkunkcraftsUpdater_lin from the zip, which needs nothing at all.

What it writes
--------------

Inside each add-on folder: skunkcrafts_updater.cfg stays where it is and
remains the add-on's configuration (aircraft read their version there); a
"skunkcrafts" sub-folder holds the cache that makes checks fast and your beta
channels (leave the "channels" folder and channel.cfg alone). Inside
each X-Plane folder: Output/skunkcrafts/updater (log, cache, reports). Your own
preferences: in your user profile (AppData on Windows, Library/Application
Support on macOS, ~/.config on Linux).

Updates of the updater
----------------------

At startup the program checks whether a newer version of itself has been
published (at most once an hour). If so, a banner offers to show what
changed and to install it: the running program is replaced in place, then
"Restart now" starts the new version (on Windows, the installed edition
downloads the new setup and runs it instead). You can turn the check off in
Options. Details in USER_GUIDE.html, section 9.

Command line
------------

"sku" (sku.exe on Windows) is the same updater without a window:

     sku instances                    X-Plane installations found
     sku --xp DIR list                add-ons of an installation
     sku --xp DIR check               check for updates
     sku --xp DIR update [name]       update / repair
     sku --xp DIR report [name]       last report
     sku --xp DIR changelog [name]    what changed
     sku --xp DIR channels [name]     beta channels of each add-on
     sku client [download [DIR]]      is a newer updater published?

The full user guide is USER_GUIDE.html, next to this file: open it in any browser.

Support: the log file is Output/skunkcrafts/updater/updater_log.txt in your
X-Plane folder. Third-party components and their licenses are listed in
THIRD_PARTY_NOTICES.txt.

(c) 2025-2026 Lionel Zamouth - https://www.skunkcrafts.com/
