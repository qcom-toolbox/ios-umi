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
	fprintf(stderr, "pixels: %ld bytes, %ld non-zero\n", (long)n, (long)nz);
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

int
main(int argc, char **argv)
{
	setvbuf(stderr, NULL, _IONBF, 0);
	if (access("/var/tmp/sweet-heic-ms.run", F_OK) == 0 && getenv("SWEET_HEIC_TRACED") == NULL) {
		unlink("/var/tmp/sweet-heic-ms.run");
		vt_check(DEFAULT_IMG);  /* saves the hvcC */
		ms_test();
		return 0;
	}
	if (getenv("SWEET_HEIC_TRACED") == NULL) {
		/* one traced child per pass: default (remote decoder), then VideoToolbox restrictions that run the
		 * decoder in this process (0x8) and also avoid hardware transfer / decoders (0xb) */
		static const char *passes[] = { "0" };
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
