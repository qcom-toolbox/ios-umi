/*
 * libsweet-heic-trace.dylib: inserted (DYLD_INSERT_LIBRARIES) into sweet-heic-trace to see where a HEIC
 * decode fails on umi (no AppleM2ScalerCSCDriver, no AppleVXD). dyld interposing: every call below is logged
 * with its result and the caller chain as shared-cache addresses with the slide removed (symbolicate on the
 * Mac with `ipsw dsc a2s research/dsc/.../dyld_shared_cache_arm64 ADDR`).
 */
#include <CoreFoundation/CoreFoundation.h>
#include <CoreVideo/CoreVideo.h>
#include <IOSurface/IOSurfaceRef.h>
#include <VideoToolbox/VideoToolbox.h>
#include <execinfo.h>
#include <mach-o/dyld.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct __IOSurfaceAccelerator *IOSurfaceAcceleratorRef;
extern int IOSurfaceAcceleratorCreate(CFAllocatorRef a, CFDictionaryRef opts, IOSurfaceAcceleratorRef *out);
extern int IOSurfaceAcceleratorTransferSurface(IOSurfaceAcceleratorRef acc, IOSurfaceRef src, IOSurfaceRef dst,
    CFDictionaryRef opts, void *a4, void *a5, void *a6);
extern OSStatus FigSignalErrorAt(OSStatus err, void *a1, void *a2, void *a3, void *a4, void *a5, void *a6,
    uint32_t a7);

static intptr_t
dsc_slide(void)
{
	static intptr_t slide = -1;
	if (slide == -1) {
		slide = 0;
		for (uint32_t i = 0; i < _dyld_image_count(); i++) {
			const char *n = _dyld_get_image_name(i);
			if (n != NULL && strstr(n, "CoreFoundation.framework") != NULL) {
				slide = _dyld_get_image_vmaddr_slide(i);
				break;
			}
		}
	}
	return slide;
}

void trace_stack(void);
void
trace_stack(void)
{
	void *f[24];
	int n = backtrace(f, 24);
	fprintf(stderr, "   bt");
	for (int i = 1; i < n; i++) {
		fprintf(stderr, " 0x%llx", (unsigned long long)((uintptr_t)f[i] - (uintptr_t)dsc_slide()));
	}
	fprintf(stderr, "\n");
}

static int
t_IOSurfaceAcceleratorCreate(CFAllocatorRef a, CFDictionaryRef opts, IOSurfaceAcceleratorRef *out)
{
	int r = IOSurfaceAcceleratorCreate(a, opts, out);
	fprintf(stderr, "[trace] IOSurfaceAcceleratorCreate -> 0x%x acc=%p\n", r, out ? (void *)*out : NULL);
	trace_stack();
	return r;
}

static int
t_IOSurfaceAcceleratorTransferSurface(IOSurfaceAcceleratorRef acc, IOSurfaceRef src, IOSurfaceRef dst,
    CFDictionaryRef opts, void *a4, void *a5, void *a6)
{
	int r = IOSurfaceAcceleratorTransferSurface(acc, src, dst, opts, a4, a5, a6);
	fprintf(stderr, "[trace] IOSurfaceAcceleratorTransferSurface(acc=%p, %#x -> %#x) -> 0x%x\n", (void *)acc,
	    src ? (unsigned)IOSurfaceGetPixelFormat(src) : 0, dst ? (unsigned)IOSurfaceGetPixelFormat(dst) : 0, r);
	trace_stack();
	return r;
}

static OSStatus
t_FigSignalErrorAt(OSStatus err, void *a1, void *a2, void *a3, void *a4, void *a5, void *a6, uint32_t a7)
{
	if (err != 0) {
		fprintf(stderr, "[trace] FigSignalErrorAt err=%d\n", (int)err);
		trace_stack();
	}
	return FigSignalErrorAt(err, a1, a2, a3, a4, a5, a6, a7);
}

static OSStatus
t_VTPixelTransferSessionTransferImage(VTPixelTransferSessionRef s, CVPixelBufferRef src, CVPixelBufferRef dst)
{
	OSStatus r = VTPixelTransferSessionTransferImage(s, src, dst);
	fprintf(stderr, "[trace] VTPixelTransferSessionTransferImage(%#x %zux%zu -> %#x %zux%zu) -> %d\n",
	    src ? (unsigned)CVPixelBufferGetPixelFormatType(src) : 0, src ? CVPixelBufferGetWidth(src) : 0,
	    src ? CVPixelBufferGetHeight(src) : 0, dst ? (unsigned)CVPixelBufferGetPixelFormatType(dst) : 0,
	    dst ? CVPixelBufferGetWidth(dst) : 0, dst ? CVPixelBufferGetHeight(dst) : 0, (int)r);
	if (r != 0) {
		trace_stack();
	}
	return r;
}

static OSStatus
t_VTDecompressionSessionCreate(CFAllocatorRef a, CMVideoFormatDescriptionRef fd, CFDictionaryRef spec,
    CFDictionaryRef attrs, const VTDecompressionOutputCallbackRecord *cb, VTDecompressionSessionRef *out)
{
	OSStatus r = VTDecompressionSessionCreate(a, fd, spec, attrs, cb, out);
	fprintf(stderr, "[trace] VTDecompressionSessionCreate(codec '%.4s') -> %d\n",
	    fd ? (const char *)&(FourCharCode){CFSwapInt32HostToBig(CMFormatDescriptionGetMediaSubType(fd))} : "----",
	    (int)r);
	trace_stack();
	return r;
}


#include <mach/message.h>
static mach_msg_return_t
t_mach_msg(mach_msg_header_t *msg, mach_msg_option_t opt, mach_msg_size_t ssz, mach_msg_size_t rsz,
    mach_port_name_t rn, mach_msg_timeout_t to, mach_port_name_t notify)
{
	mach_msg_id_t id = msg ? msg->msgh_id : 0;
	mach_msg_return_t r = mach_msg(msg, opt, ssz, rsz, rn, to, notify);
	if (r != MACH_MSG_SUCCESS && r != MACH_RCV_TIMED_OUT) {
		fprintf(stderr, "[trace] mach_msg id %d opt %#x -> %#x\n", id, opt, r);
		trace_stack();
	}
	return r;
}

#include <xpc/xpc.h>
static xpc_object_t
t_xpc_sync(xpc_connection_t c, xpc_object_t m)
{
	xpc_object_t r = xpc_connection_send_message_with_reply_sync(c, m);
	const char *name = xpc_connection_get_name(c);
	if (name != NULL && strstr(name, "coremedia") != NULL) {
		char *dm = xpc_copy_description(m), *dr = r ? xpc_copy_description(r) : NULL;
		fprintf(stderr, "[xpc] %s\n  sent: %.700s\n  reply: %.700s\n", name, dm ? dm : "?", dr ? dr : "(null)");
		free(dm);
		free(dr);
	}
	return r;
}

__attribute__((used, section("__DATA,__interpose"))) static const struct {
	const void *replacement, *original;
} sweet_heic_interpose[] = {
	{ (const void *)t_IOSurfaceAcceleratorCreate, (const void *)IOSurfaceAcceleratorCreate },
	{ (const void *)t_IOSurfaceAcceleratorTransferSurface, (const void *)IOSurfaceAcceleratorTransferSurface },
	{ (const void *)t_FigSignalErrorAt, (const void *)FigSignalErrorAt },
	{ (const void *)t_VTPixelTransferSessionTransferImage, (const void *)VTPixelTransferSessionTransferImage },
	{ (const void *)t_VTDecompressionSessionCreate, (const void *)VTDecompressionSessionCreate },
	{ (const void *)t_mach_msg, (const void *)mach_msg },
	{ (const void *)t_xpc_sync, (const void *)xpc_connection_send_message_with_reply_sync },
};
