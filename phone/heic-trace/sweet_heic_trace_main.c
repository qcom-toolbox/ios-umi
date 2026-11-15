/*
 * sweet-heic-trace: decode one image with ImageIO the way CoreAnimation does (CGImageSource -> CGImage ->
 * pixels) and say which step fails. Re-executes itself once with DYLD_INSERT_LIBRARIES pointing at
 * libsweet-heic-trace.dylib (call trace). Default image: the iOS 16 wallpaper's Head.HEIC.
 */
#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>
#include <VideoToolbox/VideoToolbox.h>
#include <IOSurface/IOSurfaceRef.h>
#include <CoreVideo/CoreVideo.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

#define TRACE_LIB "/usr/local/lib/libsweet-heic-trace.dylib"
#define DEFAULT_IMG \
	"/Library/Wallpaper/Collections/iOS_16~iphone.wallpaperCollection/Wallpapers/" \
	"7400.WWDC_2022-375w-812h@3x~iphone.wallpaper/7400.WWDC_2022_Background-375w-812h@3x~iphone.ca/assets/Head.HEIC"

extern char **environ;
extern OSStatus VTApplyRestrictions(uint32_t flags);
#include <dlfcn.h>
#include <fts.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <sys/ioccom.h>
#include <errno.h>
#include <mach/mach.h>
extern mach_port_t bootstrap_port;
extern kern_return_t bootstrap_look_up(mach_port_t bp, const char *name, mach_port_t *sp);
#include <signal.h>
#include <sys/sysctl.h>
void sweet_heic_wrap_cmphoto(void);

static int
decode(const char *path)
{
	fprintf(stderr, "== %s\n", path);
	CFURLRef url = CFURLCreateFromFileSystemRepresentation(NULL, (const UInt8 *)path, (CFIndex)strlen(path), false);
	CGImageSourceRef src = url ? CGImageSourceCreateWithURL(url, NULL) : NULL;
	if (url) {
		CFRelease(url);
	}
	if (src == NULL) {
		fprintf(stderr, "source: NULL\n");
		return 1;
	}
	char type[64] = "?";
	if (CGImageSourceGetType(src)) {
		CFStringGetCString(CGImageSourceGetType(src), type, sizeof(type), kCFStringEncodingUTF8);
	}
	fprintf(stderr, "source: type %s, %zu image(s)\n", type, CGImageSourceGetCount(src));
	const void *k[] = { kCGImageSourceShouldCacheImmediately };
	const void *v[] = { kCFBooleanTrue };
	CFDictionaryRef opts = CFDictionaryCreate(NULL, k, v, 1, &kCFTypeDictionaryKeyCallBacks,
	    &kCFTypeDictionaryValueCallBacks);
	fprintf(stderr, "-- CGImageSourceCreateImageAtIndex (decode now)\n");
	CGImageRef img = CGImageSourceCreateImageAtIndex(src, 0, opts);
	CFRelease(opts);
	CFRelease(src);
	if (img == NULL) {
		fprintf(stderr, "image: NULL\n");
		return 1;
	}
	fprintf(stderr, "image: %zux%zu bpc %zu bpp %zu\n", CGImageGetWidth(img), CGImageGetHeight(img),
	    CGImageGetBitsPerComponent(img), CGImageGetBitsPerPixel(img));
	{
		/* the IOSurface behind the image, if any (CoreAnimation / Photos texture it directly) */
		extern CFTypeRef CGImageGetProperty(CGImageRef, CFStringRef);
		extern const CFStringRef kCGImagePropertyIOSurface;
		IOSurfaceRef sf = (IOSurfaceRef)CGImageGetProperty(img, kCGImagePropertyIOSurface);
		if (sf == NULL) {
			fprintf(stderr, "image IOSurface: none\n");
		} else {
			OSType pf = IOSurfaceGetPixelFormat(sf);
			fprintf(stderr, "image IOSurface: %.4s %zux%zu bpr %zu planes %zu", (const char *)&(OSType){CFSwapInt32HostToBig(pf)},
			    IOSurfaceGetWidth(sf), IOSurfaceGetHeight(sf), IOSurfaceGetBytesPerRow(sf), IOSurfaceGetPlaneCount(sf));
			for (size_t i = 0; i < IOSurfaceGetPlaneCount(sf); i++)
				fprintf(stderr, " | p%zu %zux%zu bpr %zu bpe %zu", i, IOSurfaceGetWidthOfPlane(sf, i),
				    IOSurfaceGetHeightOfPlane(sf, i), IOSurfaceGetBytesPerRowOfPlane(sf, i),
				    IOSurfaceGetBytesPerElementOfPlane(sf, i));
			fprintf(stderr, "\n");
		}
	}
	fprintf(stderr, "-- CGDataProviderCopyData\n");
	CFDataRef d = CGDataProviderCopyData(CGImageGetDataProvider(img));
	if (d == NULL) {
		fprintf(stderr, "pixels: NULL\n");
		CGImageRelease(img);
		return 1;
	}
	CFIndex n = CFDataGetLength(d), nz = 0;
	const UInt8 *b = CFDataGetBytePtr(d);
	for (CFIndex i = 0; i < n; i++) {
		nz += b[i] != 0;
	}
	static uint8_t *prev;
	static CFIndex prevn;
	if (prev != NULL && prevn == n) {
		size_t bpr = CGImageGetBytesPerRow(img), diff = 0, r0 = (size_t)-1, r1 = 0, rows = 0, lastrow = (size_t)-1;
		for (CFIndex i = 0; i < n; i++)
			if (prev[i] != b[i]) {
				size_t r = (size_t)i / bpr;
				diff++;
				if (r < r0)
					r0 = r;
				if (r > r1)
					r1 = r;
				if (r != lastrow) {
					rows++;
					lastrow = r;
				}
			}
		fprintf(stderr, "vs previous decode: %zu bytes differ in %zu rows (%zu..%zu), bpr %zu\n", diff, rows,
		    diff ? r0 : 0, r1, bpr);
	}
	{
		/* raw RGBA of each decode for a look on the Mac: /var/tmp/sweet-heic-decode-N.raw */
		static int dumpno;
		char dp[64];
		snprintf(dp, sizeof(dp), "/var/tmp/sweet-heic-decode-%d.raw", dumpno++);
		FILE *df = fopen(dp, "wb");
		if (df != NULL) {
			fwrite(b, 1, (size_t)n, df);
			fclose(df);
		}
		fprintf(stderr, "raw %s: %zux%zu bpr %zu bitmapinfo %#x\n", dp, CGImageGetWidth(img), CGImageGetHeight(img),
		    CGImageGetBytesPerRow(img), (unsigned)CGImageGetBitmapInfo(img));
	}
	free(prev);
	prev = malloc((size_t)n);
	memcpy(prev, b, (size_t)n);
	prevn = n;
	uint64_t h = 1469598103934665603ULL;
	for (CFIndex i = 0; i < n; i++)
		h = (h ^ b[i]) * 1099511628211ULL;
	fprintf(stderr, "pixels: %ld bytes, %ld non-zero, fnv %016llx\n", (long)n, (long)nz, (unsigned long long)h);
	CFRelease(d);
	CGImageRelease(img);
	return nz ? 0 : 1;
}


/* What IOSurface / CoreVideo accept in this process: limits, and the buffers a HEIC decode makes. */
static void
cv_check(void)
{
	fprintf(stderr, "== IOSurface limits: width %zu height %zu bytesPerRow %zu, alignment bytesPerRow %zu\n",
	    IOSurfaceGetPropertyMaximum(kIOSurfaceWidth), IOSurfaceGetPropertyMaximum(kIOSurfaceHeight),
	    IOSurfaceGetPropertyMaximum(kIOSurfaceBytesPerRow), IOSurfaceGetPropertyAlignment(kIOSurfaceBytesPerRow));
	static const OSType fmts[] = { 'BGRA', '420f', '420v', 'RGhA', 'x420', '&8v0', 'L008' };
	static const int sizes[][2] = { { 512, 512 }, { 1105, 1160 }, { 553, 580 } };
	for (unsigned f = 0; f < sizeof(fmts) / sizeof(fmts[0]); f++) {
		for (unsigned z = 0; z < 3; z++) {
			CFDictionaryRef ios = CFDictionaryCreate(NULL, NULL, NULL, 0, &kCFTypeDictionaryKeyCallBacks,
			    &kCFTypeDictionaryValueCallBacks);
			const void *k[] = { kCVPixelBufferIOSurfacePropertiesKey, kCVPixelBufferMetalCompatibilityKey };
			const void *v[] = { ios, kCFBooleanTrue };
			CFDictionaryRef a = CFDictionaryCreate(NULL, k, v, 2, &kCFTypeDictionaryKeyCallBacks,
			    &kCFTypeDictionaryValueCallBacks);
			CVPixelBufferRef pb = NULL;
			CVReturn r = CVPixelBufferCreate(NULL, sizes[z][0], sizes[z][1], fmts[f], a, &pb);
			CVPixelBufferRef pb2 = NULL;
			CVReturn r2 = CVPixelBufferCreate(NULL, sizes[z][0], sizes[z][1], fmts[f], NULL, &pb2);
			fprintf(stderr, "CVPixelBufferCreate %.4s %dx%d: IOSurface-backed -> %d%s, plain -> %d\n",
			    (const char *)&(OSType){CFSwapInt32HostToBig(fmts[f])}, sizes[z][0], sizes[z][1], r,
			    pb && CVPixelBufferGetIOSurface(pb) ? " (surface)" : "", r2);
			IOSurfaceRef sf = pb ? CVPixelBufferGetIOSurface(pb) : NULL;
			if (sf != NULL) {
				size_t np = IOSurfaceGetPlaneCount(sf);
				uintptr_t base = (uintptr_t)IOSurfaceGetBaseAddress(sf);
				fprintf(stderr, "   surface bpr %zu base %%64=%lu planes %zu", IOSurfaceGetBytesPerRow(sf),
				    (unsigned long)(base & 63), np);
				for (size_t i = 0; i < np; i++)
					fprintf(stderr, " | plane %zu %zux%zu bpr %zu off %#lx", i, IOSurfaceGetWidthOfPlane(sf, i),
					    IOSurfaceGetHeightOfPlane(sf, i), IOSurfaceGetBytesPerRowOfPlane(sf, i),
					    (unsigned long)((uintptr_t)IOSurfaceGetBaseAddressOfPlane(sf, i) - base));
				fprintf(stderr, "\n");
			}
			if (pb)
				CFRelease(pb);
			if (pb2)
				CFRelease(pb2);
			CFRelease(a);
			CFRelease(ios);
		}
	}
}

/* VideoToolbox directly: which HEVC decoders are registered, and can a decoder session be made from the image's
 * own hvcC (HEVC decoder configuration) box. */
static void
vt_check(const char *path)
{
	static const char *svc[] = { "com.apple.coremedia.decompressionsession",
		"com.apple.coremedia.decompressionsession.xpc", "com.apple.coremedia.compressionsession",
		"com.apple.coremedia.formatreader.xpc" };
	for (unsigned i = 0; i < sizeof(svc) / sizeof(svc[0]); i++) {
		mach_port_t port = MACH_PORT_NULL;
		kern_return_t kr = bootstrap_look_up(bootstrap_port, svc[i], &port);
		fprintf(stderr, "bootstrap_look_up %s -> %#x port %#x\n", svc[i], kr, port);
	}
	fprintf(stderr, "== VideoToolbox: hardware HEVC decode supported: %d\n",
	    (int)VTIsHardwareDecodeSupported(kCMVideoCodecType_HEVC));
	CFArrayRef list = NULL;
	OSStatus (*copy_list)(CFDictionaryRef, CFArrayRef *) = (OSStatus(*)(CFDictionaryRef, CFArrayRef *))dlsym(RTLD_DEFAULT,
	    "VTCopyVideoDecoderList");
	if (copy_list != NULL && copy_list(NULL, &list) == 0 && list != NULL) {
		for (CFIndex i = 0; i < CFArrayGetCount(list); i++) {
			CFDictionaryRef d = CFArrayGetValueAtIndex(list, i);
			char codec[16] = "", name[128] = "", id[128] = "";
			CFTypeRef c = CFDictionaryGetValue(d, CFSTR("CodecType"));
			if (c != NULL) {
				uint32_t t = 0;
				CFNumberGetValue(c, kCFNumberSInt32Type, &t);
				snprintf(codec, sizeof(codec), "%c%c%c%c", t >> 24, t >> 16, t >> 8, t);
			}
			if (CFDictionaryGetValue(d, CFSTR("CodecName")))
				CFStringGetCString(CFDictionaryGetValue(d, CFSTR("CodecName")), name, sizeof(name),
				    kCFStringEncodingUTF8);
			if (CFDictionaryGetValue(d, CFSTR("DecoderID")))
				CFStringGetCString(CFDictionaryGetValue(d, CFSTR("DecoderID")), id, sizeof(id),
				    kCFStringEncodingUTF8);
			CFTypeRef hw = CFDictionaryGetValue(d, CFSTR("IsHardwareAccelerated"));
			if (strncmp(codec, "hvc", 3) == 0 || strstr(id, "hevc") || strstr(id, "HEVC"))
				fprintf(stderr, "decoder: %s %s %s hw=%d\n", codec, name, id, hw ? (int)CFBooleanGetValue(hw) : -1);
		}
		fprintf(stderr, "decoders listed: %ld\n", (long)CFArrayGetCount(list));
		CFRelease(list);
	} else {
		fprintf(stderr, "VTCopyVideoDecoderList failed\n");
	}
	FILE *f = fopen(path, "rb");
	if (f == NULL)
		return;
	static uint8_t buf[1 << 20];
	size_t n = fread(buf, 1, sizeof(buf), f);
	fclose(f);
	for (size_t i = 4; i + 4 < n; i++) {
		if (memcmp(buf + i, "hvcC", 4) != 0)
			continue;
		uint32_t sz = (uint32_t)buf[i - 4] << 24 | buf[i - 3] << 16 | buf[i - 2] << 8 | buf[i - 1];
		if (sz < 8 || i - 4 + sz > n)
			continue;
		CFDataRef hvcc = CFDataCreate(NULL, buf + i + 4, sz - 8);
		FILE *hf = fopen("/var/tmp/sweet-hvcc.bin", "wb");
		if (hf != NULL) {
			fwrite(buf + i + 4, 1, sz - 8, hf);
			fclose(hf);
		}
		const void *ak[] = { CFSTR("hvcC") }, *av[] = { hvcc };
		CFDictionaryRef atoms = CFDictionaryCreate(NULL, ak, av, 1, &kCFTypeDictionaryKeyCallBacks,
		    &kCFTypeDictionaryValueCallBacks);
		const void *ek[] = { kCMFormatDescriptionExtension_SampleDescriptionExtensionAtoms }, *ev[] = { atoms };
		CFDictionaryRef ext = CFDictionaryCreate(NULL, ek, ev, 1, &kCFTypeDictionaryKeyCallBacks,
		    &kCFTypeDictionaryValueCallBacks);
		CMVideoFormatDescriptionRef fd = NULL;
		OSStatus st = CMVideoFormatDescriptionCreate(NULL, kCMVideoCodecType_HEVC, 512, 512, ext, &fd);
		fprintf(stderr, "hvcC %u bytes; format description -> %d\n", sz - 8, (int)st);
		if (st == 0) {
			VTDecompressionSessionRef s = NULL;
			CFAbsoluteTime t0 = CFAbsoluteTimeGetCurrent();
			st = VTDecompressionSessionCreate(NULL, fd, NULL, NULL, NULL, &s);
			fprintf(stderr, "VTDecompressionSessionCreate (default) -> %d after %.3f s\n", (int)st,
			    CFAbsoluteTimeGetCurrent() - t0);
			if (s) {
				CFBooleanRef hw = NULL;
				VTSessionCopyProperty(s, CFSTR("UsingHardwareAcceleratedVideoDecoder"), NULL, &hw);
				fprintf(stderr, "  using hardware decoder: %d\n", hw ? (int)CFBooleanGetValue(hw) : -1);
				VTDecompressionSessionInvalidate(s);
				CFRelease(s);
			}
			{
				/* IOSurface-backed output, as the remote path always asks for */
				CFDictionaryRef ios = CFDictionaryCreate(NULL, NULL, NULL, 0, &kCFTypeDictionaryKeyCallBacks,
				    &kCFTypeDictionaryValueCallBacks);
				const void *ak2[] = { kCVPixelBufferIOSurfacePropertiesKey }, *av2[] = { ios };
				CFDictionaryRef attrs = CFDictionaryCreate(NULL, ak2, av2, 1, &kCFTypeDictionaryKeyCallBacks,
				    &kCFTypeDictionaryValueCallBacks);
				VTDecompressionSessionRef s2 = NULL;
				OSStatus st2 = VTDecompressionSessionCreate(NULL, fd, NULL, attrs, NULL, &s2);
				fprintf(stderr, "VTDecompressionSessionCreate (IOSurface output) -> %d\n", (int)st2);
				if (s2) {
					VTDecompressionSessionInvalidate(s2);
					CFRelease(s2);
				}
				CFRelease(attrs);
				CFRelease(ios);
			}
			const void *sk[] = { CFSTR("EnableHardwareAcceleratedVideoDecoder") };
			const void *sv[] = { kCFBooleanFalse };
			CFDictionaryRef spec = CFDictionaryCreate(NULL, sk, sv, 1, &kCFTypeDictionaryKeyCallBacks,
			    &kCFTypeDictionaryValueCallBacks);
			s = NULL;
			st = VTDecompressionSessionCreate(NULL, fd, spec, NULL, NULL, &s);
			fprintf(stderr, "VTDecompressionSessionCreate (software only) -> %d\n", (int)st);
			if (s) {
				VTDecompressionSessionInvalidate(s);
				CFRelease(s);
			}
			CFRelease(spec);
			CFRelease(fd);
		}
		CFRelease(ext);
		CFRelease(atoms);
		CFRelease(hvcc);
		break;
	}
}

/* --ms: load libsweet-heic-mstest.dylib into mediaserverd (kern.exec_inject + restart), wait, restore. */
#define MS_LIB "/usr/local/lib/libsweet-heic-mstest.dylib"
static void
ms_test(void)
{
	char old[512] = "", now[600];
	size_t len = sizeof(old);
	if (sysctlbyname("kern.exec_inject", old, &len, NULL, 0) != 0) {
		fprintf(stderr, "kern.exec_inject: read failed\n");
		return;
	}
	snprintf(now, sizeof(now), "%s%s" MS_LIB " mediaserverd", old, old[0] ? ";" : "");
	int r = sysctlbyname("kern.exec_inject", NULL, NULL, now, strlen(now) + 1);
	fprintf(stderr, "kern.exec_inject '%s' -> %d\n", now, r);
	pid_t pids[16];
	int n = 0;
	int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_ALL, 0 };
	size_t sz = 0;
	sysctl(mib, 3, NULL, &sz, NULL, 0);
	struct kinfo_proc *kp = malloc(sz);
	if (kp && sysctl(mib, 3, kp, &sz, NULL, 0) == 0) {
		for (size_t i = 0; i < sz / sizeof(*kp) && n < 16; i++)
			if (strcmp(kp[i].kp_proc.p_comm, "mediaserverd") == 0)
				pids[n++] = kp[i].kp_proc.p_pid;
	}
	free(kp);
	for (int i = 0; i < n; i++)
		fprintf(stderr, "kill mediaserverd %d -> %d\n", pids[i], kill(pids[i], SIGKILL));
	sleep(25);
	r = sysctlbyname("kern.exec_inject", NULL, NULL, old, strlen(old) + 1);
	fprintf(stderr, "kern.exec_inject restored -> %d\n", r);
}

/* /var/tmp/sweet-inject.run holds "LIBRARY NAME [NAME...]": add that group to kern.exec_inject (until the next boot)
 * and kill the named programs so launchd starts them again with the library. */
static void
inject_run(void)
{
	FILE *f = fopen("/var/tmp/sweet-inject.run", "r");
	char line[400] = "";
	if (f == NULL)
		return;
	if (fgets(line, sizeof(line), f) == NULL)
		line[0] = 0;
	fclose(f);
	unlink("/var/tmp/sweet-inject.run");
	line[strcspn(line, "\n")] = 0;
	char old[512] = "", now[1000];
	size_t len = sizeof(old);
	sysctlbyname("kern.exec_inject", old, &len, NULL, 0);
	snprintf(now, sizeof(now), "%s%s%s", old, old[0] ? ";" : "", line);
	fprintf(stderr, "kern.exec_inject '%s' -> %d\n", now, sysctlbyname("kern.exec_inject", NULL, NULL, now, strlen(now) + 1));
	int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_ALL, 0 };
	size_t sz = 0;
	sysctl(mib, 3, NULL, &sz, NULL, 0);
	struct kinfo_proc *kp = malloc(sz);
	if (kp && sysctl(mib, 3, kp, &sz, NULL, 0) == 0) {
		for (size_t i = 0; i < sz / sizeof(*kp); i++) {
			const char *w = strchr(line, ' ');
			while (w != NULL) {
				w++;
				size_t wl = strcspn(w, " ");
				if (wl && strlen(kp[i].kp_proc.p_comm) == wl && strncmp(kp[i].kp_proc.p_comm, w, wl) == 0)
					fprintf(stderr, "kill %s %d -> %d\n", kp[i].kp_proc.p_comm, kp[i].kp_proc.p_pid,
					    kill(kp[i].kp_proc.p_pid, SIGKILL));
				w = strchr(w, ' ');
			}
		}
	}
	free(kp);
}

/* /var/tmp/sweet-du.run: free space on / and the size of the biggest directories two levels under a few roots. */
static unsigned long long
dir_bytes(const char *path)
{
	char *const roots[] = { (char *)path, NULL };
	unsigned long long total = 0;
	FTS *fts = fts_open(roots, FTS_PHYSICAL | FTS_NOCHDIR | FTS_XDEV, NULL);
	FTSENT *e;
	while (fts != NULL && (e = fts_read(fts)) != NULL)
		if (e->fts_info == FTS_F && e->fts_statp != NULL)
			total += (unsigned long long)e->fts_statp->st_blocks * 512;
	if (fts != NULL)
		fts_close(fts);
	return total;
}

static void
du_run(void)
{
	unlink("/var/tmp/sweet-du.run");
	struct statfs sf;
	if (statfs("/", &sf) == 0)
		fprintf(stderr, "/: %llu MB free of %llu MB\n", (unsigned long long)sf.f_bavail * sf.f_bsize >> 20,
		    (unsigned long long)sf.f_blocks * sf.f_bsize >> 20);
	static const char *roots[] = { "/private/var", "/private/var/mobile", "/private/var/mobile/Library",
		"/private/var/mobile/Media", "/private/var/local", "/private/var/db", "/private/var/containers/Data/Application",
		"/private/var/mobile/Containers/Data/Application", "/private/var/mobile/Library/Logs", "/private/var/tmp",
		"/private/var/root", "/private/var/jb", "/usr/local", "/var/cache", "/private/var/log" };
	for (unsigned r = 0; r < sizeof(roots) / sizeof(roots[0]); r++) {
		char *const one[] = { (char *)roots[r], NULL };
		FTS *fts = fts_open(one, FTS_PHYSICAL | FTS_NOCHDIR | FTS_XDEV, NULL);
		FTSENT *e;
		fprintf(stderr, "== %s\n", roots[r]);
		while (fts != NULL && (e = fts_read(fts)) != NULL) {
			if (e->fts_level == 1 && e->fts_info == FTS_D) {
				unsigned long long b = dir_bytes(e->fts_path);
				if (b >= (20ULL << 20))
					fprintf(stderr, "%8llu MB %s\n", b >> 20, e->fts_path);
				fts_set(fts, e, FTS_SKIP);
			} else if (e->fts_level == 1 && e->fts_info == FTS_F && e->fts_statp->st_size >= (20LL << 20)) {
				fprintf(stderr, "%8lld MB %s (file)\n", (long long)(e->fts_statp->st_size >> 20), e->fts_path);
			}
		}
		if (fts != NULL)
			fts_close(fts);
	}
}

/* /var/tmp/sweet-kill.run holds program names: SIGKILL them (launchd starts them again on demand). */
static void
kill_run(void)
{
	FILE *f = fopen("/var/tmp/sweet-kill.run", "r");
	char line[256] = "";
	if (f == NULL)
		return;
	if (fgets(line, sizeof(line), f) == NULL)
		line[0] = 0;
	fclose(f);
	unlink("/var/tmp/sweet-kill.run");
	line[strcspn(line, "\n")] = 0;
	int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_ALL, 0 };
	size_t sz = 0;
	sysctl(mib, 3, NULL, &sz, NULL, 0);
	struct kinfo_proc *kp = malloc(sz);
	if (kp && sysctl(mib, 3, kp, &sz, NULL, 0) == 0)
		for (size_t i = 0; i < sz / sizeof(*kp); i++) {
			for (char *w = line; *w;) {
				size_t wl = strcspn(w, " ");
				if (wl && strlen(kp[i].kp_proc.p_comm) == wl && strncmp(kp[i].kp_proc.p_comm, w, wl) == 0)
					fprintf(stderr, "kill %s %d -> %d\n", kp[i].kp_proc.p_comm, kp[i].kp_proc.p_pid,
					    kill(kp[i].kp_proc.p_pid, SIGKILL));
				w += wl;
				while (*w == ' ')
					w++;
			}
		}
	free(kp);
}

/*
 * /var/tmp/sweet-grow.run holds a size in GiB: grow the root HFS+ volume online to that size
 * (fsctl HFSIOC_RESIZE_VOLUME -> hfs_extendfs; journaled HFS+, root only; the userdata partition is ~107 GiB).
 */
#define SWEET_HFSIOC_RESIZE_VOLUME _IOW('h', 2, u_int64_t)
extern int fsctl(const char *path, unsigned long request, void *data, unsigned int options);
static void
grow_run(void)
{
	FILE *f = fopen("/var/tmp/sweet-grow.run", "r");
	unsigned gib = 0;
	if (f != NULL) {
		if (fscanf(f, "%u", &gib) != 1)
			gib = 0;
		fclose(f);
	}
	unlink("/var/tmp/sweet-grow.run");
	struct statfs sf;
	if (statfs("/", &sf) == 0)
		fprintf(stderr, "before: %llu MB free of %llu MB (block %u)\n", (unsigned long long)sf.f_bavail * sf.f_bsize >> 20,
		    (unsigned long long)sf.f_blocks * sf.f_bsize >> 20, (unsigned)sf.f_bsize);
	if (gib < 17 || gib > 100) {
		fprintf(stderr, "size %u GiB refused (17..100)\n", gib);
		return;
	}
	u_int64_t size = (u_int64_t)gib << 30;
	sync();
	int r = fsctl("/", SWEET_HFSIOC_RESIZE_VOLUME, &size, 0);
	fprintf(stderr, "fsctl HFSIOC_RESIZE_VOLUME %u GiB -> %d (errno %d %s)\n", gib, r, r ? errno : 0, r ? strerror(errno) : "");
	sync();
	if (statfs("/", &sf) == 0)
		fprintf(stderr, "after: %llu MB free of %llu MB\n", (unsigned long long)sf.f_bavail * sf.f_bsize >> 20,
		    (unsigned long long)sf.f_blocks * sf.f_bsize >> 20);
}

int
main(int argc, char **argv)
{
	setvbuf(stderr, NULL, _IONBF, 0);
	if (access("/var/tmp/sweet-grow.run", F_OK) == 0 && getenv("SWEET_HEIC_TRACED") == NULL) {
		grow_run();
		return 0;
	}
	if (access("/var/tmp/sweet-oslog.run", F_OK) == 0 && getenv("SWEET_HEIC_TRACED") == NULL) {
		/* stream the unified log for 2 min for the programs named in the file, via sweet-oslog-dump */
		char line[256] = "", *args[16] = { "/usr/local/bin/sweet-oslog-dump", "120" };
		FILE *f = fopen("/var/tmp/sweet-oslog.run", "r");
		if (f != NULL) {
			if (fgets(line, sizeof(line), f) == NULL)
				line[0] = 0;
			fclose(f);
		}
		unlink("/var/tmp/sweet-oslog.run");
		int na = 2;
		for (char *t = strtok(line, " \n"); t != NULL && na < 15; t = strtok(NULL, " \n"))
			args[na++] = t;
		args[na] = NULL;
		posix_spawn_file_actions_t fa;
		posix_spawn_file_actions_init(&fa);
		posix_spawn_file_actions_adddup2(&fa, 2, 1);
		pid_t pid;
		int st = 0;
		if (posix_spawn(&pid, args[0], &fa, NULL, args, environ) == 0)
			waitpid(pid, &st, 0);
		fprintf(stderr, "sweet-oslog-dump status %#x\n", st);
		return 0;
	}
	if (access("/var/tmp/sweet-kill.run", F_OK) == 0 && getenv("SWEET_HEIC_TRACED") == NULL) {
		kill_run();
		return 0;
	}
	if (access("/var/tmp/sweet-du.run", F_OK) == 0 && getenv("SWEET_HEIC_TRACED") == NULL) {
		du_run();
		return 0;
	}
	if (access("/var/tmp/sweet-inject.run", F_OK) == 0 && getenv("SWEET_HEIC_TRACED") == NULL) {
		inject_run();
		return 0;
	}
	if (access("/var/tmp/sweet-heic-ms.run", F_OK) == 0 && getenv("SWEET_HEIC_TRACED") == NULL) {
		unlink("/var/tmp/sweet-heic-ms.run");
		vt_check(DEFAULT_IMG);  /* saves the hvcC */
		ms_test();
		return 0;
	}
	if (getenv("SWEET_HEIC_TRACED") == NULL) {
		/* one traced child per pass: default (remote decoder), then VideoToolbox restrictions that run the
		 * decoder in this process (0x8) and also avoid hardware transfer / decoders (0xb) */
		static const char *passes[] = { "0x1" };
		for (unsigned p = 0; p < sizeof(passes) / sizeof(passes[0]); p++) {
			char r[64];
			snprintf(r, sizeof(r), "SWEET_HEIC_RESTRICT=%s", passes[p]);
			char *env[] = { "DYLD_INSERT_LIBRARIES=" TRACE_LIB, "SWEET_HEIC_TRACED=1", r,
				"OS_ACTIVITY_DT_MODE=YES", "CFLOG_FORCE_STDERR=1", "PATH=/usr/bin:/bin:/usr/sbin:/sbin", NULL };
			pid_t pid;
			int st = 0;
			fprintf(stderr, "######## pass %s\n", passes[p]);
			if (posix_spawn(&pid, argv[0], NULL, NULL, argv, env) == 0) {
				waitpid(pid, &st, 0);
				fprintf(stderr, "child status %#x\n", st);
			} else {
				fprintf(stderr, "spawn failed\n");
			}
		}
		return 0;
	}
	uint32_t restrict_flags = (uint32_t)strtoul(getenv("SWEET_HEIC_RESTRICT") ?: "0", NULL, 0);
	if (restrict_flags) {
		fprintf(stderr, "VTApplyRestrictions(%#x) -> %d\n", restrict_flags, (int)VTApplyRestrictions(restrict_flags));
	}
	cv_check();
	vt_check(argc > 1 ? argv[1] : DEFAULT_IMG);
	int rc = 0;
	if (argc > 1) {
		for (int i = 1; i < argc; i++) {
			rc |= decode(argv[i]);
		}
	} else {
		rc = decode(DEFAULT_IMG);
		/* ImageIO has filled its CMPhoto function pointers now: wrap them and decode again */
		sweet_heic_wrap_cmphoto();
		rc = decode(DEFAULT_IMG);
	}
	fprintf(stderr, "result: %s\n", rc ? "FAIL" : "OK");
	return rc;
}
