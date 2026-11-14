/*
 * libsweet-heic-mstest.dylib: loaded into mediaserverd (kern.exec_inject, set by sweet-heic-trace --ms), where
 * VideoToolbox's remote decoder sessions live. 5 s after start it creates an HEVC decoder session from the hvcC
 * the probe saved (/var/tmp/sweet-hvcc.bin), the way the server does, and logs the result plus every non-zero
 * FigSignalErrorAt with its caller chain to /var/tmp/sweet-heic-ms.log.
 */
#include <CoreFoundation/CoreFoundation.h>
#include <CoreMedia/CoreMedia.h>
#include <VideoToolbox/VideoToolbox.h>
#include <execinfo.h>
#include <mach-o/dyld.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

extern OSStatus FigSignalErrorAt(OSStatus err, void *a1, void *a2, void *a3, void *a4, void *a5, void *a6, uint32_t a7);
static FILE *mslog;

static intptr_t
slide(void)
{
	for (uint32_t i = 0; i < _dyld_image_count(); i++)
		if (strstr(_dyld_get_image_name(i), "CoreFoundation.framework"))
			return _dyld_get_image_vmaddr_slide(i);
	return 0;
}

static void
bt(void)
{
	void *f[20];
	int n = backtrace(f, 20);
	fprintf(mslog, "   bt");
	for (int i = 1; i < n; i++)
		fprintf(mslog, " 0x%llx", (unsigned long long)((uintptr_t)f[i] - (uintptr_t)slide()));
	fprintf(mslog, "\n");
	fflush(mslog);
}

static OSStatus
t_FigSignalErrorAt(OSStatus err, void *a1, void *a2, void *a3, void *a4, void *a5, void *a6, uint32_t a7)
{
	if (err != 0 && mslog != NULL) {
		fprintf(mslog, "FigSignalErrorAt %d\n", (int)err);
		bt();
	}
	return FigSignalErrorAt(err, a1, a2, a3, a4, a5, a6, a7);
}

__attribute__((used, section("__DATA,__interpose"))) static const struct {
	const void *r, *o;
} ms_interpose[] = { { (const void *)t_FigSignalErrorAt, (const void *)FigSignalErrorAt } };

static void *
test(void *arg)
{
	(void)arg;
	sleep(5);
	FILE *f = fopen("/var/tmp/sweet-hvcc.bin", "rb");
	static uint8_t buf[4096];
	size_t n = f ? fread(buf, 1, sizeof(buf), f) : 0;
	if (f)
		fclose(f);
	fprintf(mslog, "mediaserverd pid %d: hvcC %zu bytes\n", getpid(), n);
	if (n == 0)
		return NULL;
	CFDataRef hvcc = CFDataCreate(NULL, buf, (CFIndex)n);
	const void *ak[] = { CFSTR("hvcC") }, *av[] = { hvcc };
	CFDictionaryRef atoms = CFDictionaryCreate(NULL, ak, av, 1, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
	const void *ek[] = { kCMFormatDescriptionExtension_SampleDescriptionExtensionAtoms }, *ev[] = { atoms };
	CFDictionaryRef ext = CFDictionaryCreate(NULL, ek, ev, 1, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
	CMVideoFormatDescriptionRef fd = NULL;
	OSStatus st = CMVideoFormatDescriptionCreate(NULL, kCMVideoCodecType_HEVC, 512, 512, ext, &fd);
	fprintf(mslog, "format description -> %d\n", (int)st);
	VTDecompressionSessionRef s = NULL;
	st = VTDecompressionSessionCreate(NULL, fd, NULL, NULL, NULL, &s);
	fprintf(mslog, "VTDecompressionSessionCreate in mediaserverd -> %d\n", (int)st);
	fflush(mslog);
	if (s) {
		VTDecompressionSessionInvalidate(s);
		CFRelease(s);
	}
	return NULL;
}

__attribute__((constructor)) static void
init(void)
{
	if (strcmp(getprogname(), "mediaserverd") != 0)
		return;
	mslog = fopen("/var/tmp/sweet-heic-ms.log", "a");
	if (mslog == NULL)
		return;
	fprintf(mslog, "---- loaded into mediaserverd %d\n", getpid());
	fflush(mslog);
	pthread_t t;
	pthread_create(&t, NULL, test, NULL);
	pthread_detach(t);
}
