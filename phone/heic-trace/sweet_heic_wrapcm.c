/*
 * ImageIO reaches CMPhoto through function pointers it fills with dlsym (gFunc_CMPhoto...), kept in writable
 * data. sweet_heic_wrap_cmphoto() points every such pointer at a logging stub (up to 64), so each ImageIO ->
 * CMPhoto call prints its name and 32-bit result. Stubs pass x0-x7 through unchanged (CMPhoto's entry points take
 * pointers / integers / CFTypes, at most 8).
 */
#include <dlfcn.h>
#include <mach-o/dyld.h>
#include <mach-o/getsect.h>
#include <mach/mach.h>
#include <stdio.h>
#include <string.h>

typedef long (*fn8)(long, long, long, long, long, long, long, long);
static fn8 orig[64];
static const char *names[64];

#define STUB(i)                                                                                 \
	static long stub##i(long a0, long a1, long a2, long a3, long a4, long a5, long a6, long a7) \
	{                                                                                          \
		long r = orig[i](a0, a1, a2, a3, a4, a5, a6, a7);                                  \
		fprintf(stderr, "[cmphoto] %s -> %d\n", names[i], (int)r);                         \
		return r;                                                                          \
	}
STUB(0)
STUB(1)
STUB(2)
STUB(3)
STUB(4)
STUB(5)
STUB(6)
STUB(7)
STUB(8)
STUB(9)
STUB(10)
STUB(11)
STUB(12)
STUB(13)
STUB(14)
STUB(15)
STUB(16)
STUB(17)
STUB(18)
STUB(19)
STUB(20)
STUB(21)
STUB(22)
STUB(23)
STUB(24)
STUB(25)
STUB(26)
STUB(27)
STUB(28)
STUB(29)
STUB(30)
STUB(31)
STUB(32)
STUB(33)
STUB(34)
STUB(35)
STUB(36)
STUB(37)
STUB(38)
STUB(39)
STUB(40)
STUB(41)
STUB(42)
STUB(43)
STUB(44)
STUB(45)
STUB(46)
STUB(47)
STUB(48)
STUB(49)
STUB(50)
STUB(51)
STUB(52)
STUB(53)
STUB(54)
STUB(55)
STUB(56)
STUB(57)
STUB(58)
STUB(59)
STUB(60)
STUB(61)
STUB(62)
STUB(63)
static const void *stubs[64] = {
	stub0,
	stub1,
	stub2,
	stub3,
	stub4,
	stub5,
	stub6,
	stub7,
	stub8,
	stub9,
	stub10,
	stub11,
	stub12,
	stub13,
	stub14,
	stub15,
	stub16,
	stub17,
	stub18,
	stub19,
	stub20,
	stub21,
	stub22,
	stub23,
	stub24,
	stub25,
	stub26,
	stub27,
	stub28,
	stub29,
	stub30,
	stub31,
	stub32,
	stub33,
	stub34,
	stub35,
	stub36,
	stub37,
	stub38,
	stub39,
	stub40,
	stub41,
	stub42,
	stub43,
	stub44,
	stub45,
	stub46,
	stub47,
	stub48,
	stub49,
	stub50,
	stub51,
	stub52,
	stub53,
	stub54,
	stub55,
	stub56,
	stub57,
	stub58,
	stub59,
	stub60,
	stub61,
	stub62,
	stub63,
};

static const struct mach_header_64 *
image(const char *substr)
{
	for (uint32_t i = 0; i < _dyld_image_count(); i++) {
		const char *n = _dyld_get_image_name(i);
		if (n != NULL && strstr(n, substr) != NULL)
			return (const struct mach_header_64 *)_dyld_get_image_header(i);
	}
	return NULL;
}

void sweet_heic_wrap_cmphoto(void);
void
sweet_heic_wrap_cmphoto(void)
{
	const struct mach_header_64 *iio = image("/ImageIO.framework/ImageIO"), *cm = image("/CMPhoto.framework/CMPhoto");
	unsigned long tsize = 0, n = 0;
	uint8_t *text = cm ? getsegmentdata(cm, "__TEXT", &tsize) : NULL;
	if (iio == NULL || text == NULL) {
		fprintf(stderr, "[cmphoto] ImageIO %p / CMPhoto %p not loaded\n", (void *)iio, (void *)text);
		return;
	}
	static const char *segs[] = { "__DATA", "__DATA_DIRTY", "__AUTH", "__DATA_CONST" };
	for (unsigned s = 0; s < sizeof(segs) / sizeof(segs[0]); s++) {
		unsigned long size = 0;
		uint8_t *p = getsegmentdata(iio, segs[s], &size);
		for (unsigned long off = 0; p != NULL && off + 8 <= size && n < 64; off += 8) {
			const void **slot = (const void **)(p + off);
			uintptr_t v = (uintptr_t)*slot;
			if (v < (uintptr_t)text || v >= (uintptr_t)text + tsize)
				continue;
			Dl_info di;
			if (dladdr((void *)v, &di) == 0 || di.dli_sname == NULL || di.dli_saddr != (void *)v ||
			    strncmp(di.dli_sname, "CMPhoto", 7) != 0)
				continue;
			vm_address_t pg = (vm_address_t)slot & ~(vm_address_t)vm_page_mask;
			if (vm_protect(mach_task_self(), pg, vm_page_size, FALSE, VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY) != 0)
				continue;
			orig[n] = (fn8)v;
			names[n] = di.dli_sname;
			*slot = stubs[n];
			n++;
		}
	}
	fprintf(stderr, "[cmphoto] %lu ImageIO -> CMPhoto pointer(s) wrapped\n", n);
}
