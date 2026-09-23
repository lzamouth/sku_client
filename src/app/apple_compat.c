/* Replacement for the compiler-rt helpers behind Objective-C "@available"
 * checks (used inside SDL's Cocoa/Metal code). Our osxcross install has no
 * compiler-rt, so we provide them ourselves: the running macOS version comes
 * from sysctl kern.osproductversion (available since 10.13.4).
 * macOS only; the real app links this file on the macOS target only. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/sysctl.h>

static void os_version(uint32_t v[3])
{
	static uint32_t cached[3];
	static int have;
	if (!have) {
		char buf[64] = "";
		size_t len = sizeof buf - 1;
		if (sysctlbyname("kern.osproductversion", buf, &len, NULL, 0) == 0)
			sscanf(buf, "%u.%u.%u", &cached[0], &cached[1], &cached[2]);
		have = 1;
	}
	memcpy(v, cached, sizeof cached);
}

static int at_least(uint32_t major, uint32_t minor, uint32_t sub)
{
	uint32_t v[3];
	os_version(v);
	if (v[0] != major) return v[0] > major;
	if (v[1] != minor) return v[1] > minor;
	return v[2] >= sub;
}

/* clang >= 12 with platform-aware availability */
int __isPlatformVersionAtLeast(uint32_t platform, uint32_t major, uint32_t minor, uint32_t sub)
{
	(void)platform; /* we only ever build for macOS */
	return at_least(major, minor, sub);
}

/* older clang */
int __isOSVersionAtLeast(uint32_t major, uint32_t minor, uint32_t sub)
{
	return at_least(major, minor, sub);
}
