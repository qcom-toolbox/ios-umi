/*
 * libsweet-exc.dylib: logs every Objective-C exception a program throws (name, reason, thread's call chain as
 * shared-cache addresses with the slide removed) to /var/tmp/sweet-exc-PROGRAM.log, so an uncaught one's reason is
 * known even though the kernel's crash report only says "abort". Inserted with kern.exec_inject (runtime, by
 * sweet-heic-trace --inject).
 */
#include <CoreFoundation/CoreFoundation.h>
#include <execinfo.h>
#include <mach-o/dyld.h>
#include <objc/message.h>
#include <objc/runtime.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

typedef id (*objc_exception_preprocessor)(id exception);
extern objc_exception_preprocessor objc_setExceptionPreprocessor(objc_exception_preprocessor fn);
static objc_exception_preprocessor previous;
static FILE *excf;

static void
cfstr(CFTypeRef s, char *buf, size_t len)
{
	buf[0] = 0;
	if (s != NULL && CFGetTypeID(s) == CFStringGetTypeID())
		CFStringGetCString((CFStringRef)s, buf, (CFIndex)len, kCFStringEncodingUTF8);
}

static id
preprocess(id exc)
{
	if (excf != NULL && exc != NULL) {
		char name[256], reason[2048];
		cfstr((CFTypeRef)((id(*)(id, SEL))objc_msgSend)(exc, sel_registerName("name")), name, sizeof(name));
		cfstr((CFTypeRef)((id(*)(id, SEL))objc_msgSend)(exc, sel_registerName("reason")), reason, sizeof(reason));
		intptr_t slide = 0;
		for (uint32_t i = 0; i < _dyld_image_count(); i++)
			if (strstr(_dyld_get_image_name(i), "CoreFoundation.framework")) {
				slide = _dyld_get_image_vmaddr_slide(i);
				break;
			}
		void *f[24];
		int n = backtrace(f, 24);
		fprintf(excf, "exception %s: %s\n   bt", name, reason);
		for (int i = 1; i < n; i++)
			fprintf(excf, " 0x%llx", (unsigned long long)((uintptr_t)f[i] - (uintptr_t)slide));
		fprintf(excf, "\n");
		fflush(excf);
	}
	return previous ? previous(exc) : exc;
}

/*
 * os_log capture: _os_log_impl / _os_log_error_impl / _os_log_fault_impl calls are written to the same file as
 * "LOG type fmt | args" (arguments decoded from the os_log buffer: integers, C strings, CF objects' descriptions;
 * private values are logged too - this is a test build). At most 4000 lines per process.
 */
#include <os/log.h>
extern void _os_log_impl(void *dso, os_log_t log, os_log_type_t type, const char *format, uint8_t *buf, uint32_t size);
extern void _os_log_error_impl(void *dso, os_log_t log, os_log_type_t type, const char *format, uint8_t *buf, uint32_t size);
extern void _os_log_fault_impl(void *dso, os_log_t log, os_log_type_t type, const char *format, uint8_t *buf, uint32_t size);
static int loglines;

static void
log_capture(os_log_type_t type, const char *fmt, const uint8_t *buf, uint32_t size)
{
	if (excf == NULL || loglines >= 4000 || fmt == NULL)
		return;
	loglines++;
	fprintf(excf, "LOG %u %s |", (unsigned)type, fmt);
	if (buf != NULL && size >= 2) {
		uint32_t off = 2, n = buf[1];
		for (uint32_t i = 0; i < n && off + 2 <= size; i++) {
			uint8_t desc = buf[off], len = buf[off + 1];
			const uint8_t *d = buf + off + 2;
			if (off + 2 + len > size)
				break;
			uint8_t kind = desc >> 4;
			if (kind == 0 && len <= 8) {
				long long v = 0;
				memcpy(&v, d, len);
				fprintf(excf, " %lld", v);
			} else if ((kind == 2 || kind == 4) && len == 8) {
				const void *ptr;
				memcpy(&ptr, d, 8);
				if (ptr == NULL) {
					fprintf(excf, " (null)");
				} else if (kind == 2) {
					fprintf(excf, " \"%.300s\"", (const char *)ptr);
				} else {
					char tmp[600];
					CFStringRef ds = CFCopyDescription((CFTypeRef)ptr);
					cfstr(ds, tmp, sizeof(tmp));
					if (ds)
						CFRelease(ds);
					fprintf(excf, " <%s>", tmp);
				}
			} else {
				fprintf(excf, " [%u:%u]", desc, len);
			}
			off += 2 + len;
		}
	}
	fprintf(excf, "\n");
	fflush(excf);
}

static void
t_os_log_impl(void *dso, os_log_t log, os_log_type_t type, const char *fmt, uint8_t *buf, uint32_t size)
{
	log_capture(type, fmt, buf, size);
	_os_log_impl(dso, log, type, fmt, buf, size);
}
static void
t_os_log_error_impl(void *dso, os_log_t log, os_log_type_t type, const char *fmt, uint8_t *buf, uint32_t size)
{
	log_capture(type, fmt, buf, size);
	_os_log_error_impl(dso, log, type, fmt, buf, size);
}
static void
t_os_log_fault_impl(void *dso, os_log_t log, os_log_type_t type, const char *fmt, uint8_t *buf, uint32_t size)
{
	log_capture(type, fmt, buf, size);
	_os_log_fault_impl(dso, log, type, fmt, buf, size);
}

__attribute__((used, section("__DATA,__interpose"))) static const struct {
	const void *r, *o;
} exc_interpose[] = {
	{ (const void *)t_os_log_impl, (const void *)_os_log_impl },
	{ (const void *)t_os_log_error_impl, (const void *)_os_log_error_impl },
	{ (const void *)t_os_log_fault_impl, (const void *)_os_log_fault_impl },
};

__attribute__((constructor)) static void
init(void)
{
	char path[256];
	snprintf(path, sizeof(path), "/var/tmp/sweet-exc-%s.log", getprogname());
	excf = fopen(path, "a");
	if (excf != NULL) {
		fprintf(excf, "---- %s %d\n", getprogname(), getpid());
		fflush(excf);
	}
	previous = objc_setExceptionPreprocessor(preprocess);
}
