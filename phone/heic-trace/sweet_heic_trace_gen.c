/*
 * Pass-through tracing (part of libsweet-heic-trace.dylib) for calls with at most 8 integer / pointer register
 * arguments and no stack arguments: the original runs first with x0-x7 as received, then its 32-bit status (or
 * pointer) is logged, with the caller chain when it failed. No SDK headers here: the real prototypes differ.
 */
#include <stdio.h>

void trace_stack(void);
typedef long (*fn8)(long, long, long, long, long, long, long, long);

#define TRACE8(name, ptr_result)                                                                   \
	extern void name(void);                                                                     \
	static long t_##name(long a0, long a1, long a2, long a3, long a4, long a5, long a6, long a7) \
	{                                                                                           \
		long r = ((fn8)(void *)name)(a0, a1, a2, a3, a4, a5, a6, a7);                       \
		if (ptr_result) {                                                                   \
			fprintf(stderr, "[trace] %s -> %p\n", #name, (void *)r);                     \
			if (r == 0)                                                                 \
				trace_stack();                                                      \
		} else {                                                                            \
			fprintf(stderr, "[trace] %s -> %d\n", #name, (int)r);                         \
			if ((int)r != 0)                                                            \
				trace_stack();                                                      \
		}                                                                                   \
		return r;                                                                           \
	}

TRACE8(VTDecompressionSessionCreateWithOptions, 0)
TRACE8(VTDecompressionSessionDecodeFrame, 0)
TRACE8(VTDecompressionSessionDecodeFrameWithOutputHandler, 0)
TRACE8(VTDecompressionSessionWaitForAsynchronousFrames, 0)
TRACE8(VTTileDecompressionSessionCreate, 0)
TRACE8(VTTileDecompressionSessionDecodeTile, 0)
TRACE8(VTSelectAndCreateVideoDecoderInstance, 0)
TRACE8(VTDecoderSessionEmitDecodedFrame, 0)
TRACE8(VTDecoderSessionEmitDecodedTile, 0)
TRACE8(VTDecoderSessionCreatePixelBuffer, 0)
TRACE8(CVPixelBufferCreate, 0)
TRACE8(CVPixelBufferPoolCreatePixelBuffer, 0)
TRACE8(IOSurfaceCreate, 1)

#define I8(name) { (const void *)t_##name, (const void *)name },
__attribute__((used, section("__DATA,__interpose"))) static const struct {
	const void *replacement, *original;
} sweet_heic_interpose_gen[] = {
	I8(VTDecompressionSessionCreateWithOptions) I8(VTDecompressionSessionDecodeFrame)
	I8(VTDecompressionSessionDecodeFrameWithOutputHandler)
	I8(VTDecompressionSessionWaitForAsynchronousFrames)
	I8(VTTileDecompressionSessionCreate)
	I8(VTTileDecompressionSessionDecodeTile) I8(VTSelectAndCreateVideoDecoderInstance)
	I8(VTDecoderSessionEmitDecodedFrame) I8(VTDecoderSessionEmitDecodedTile) I8(VTDecoderSessionCreatePixelBuffer)
	I8(CVPixelBufferCreate) I8(CVPixelBufferPoolCreatePixelBuffer)
	I8(IOSurfaceCreate)
};

