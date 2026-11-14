/*
 * libsweet-sb.dylib (umi): moves the status bar's leading items (the clock, the call / recording pill) right of
 * the Mi 10's front camera hole. iOS lays the bar out for the iPhone X (UIKit _UIStatusBarVisualProvider_Split1125:
 * the clock centred in the left "ear" beside a 209 pt notch, at about x 41 pt); the Mi 10's hole is at panel
 * (133, 58) px, radius 31 (LineageOS umi overlay), which is x 35..57 pt of the 375 pt screen once the kernel scales
 * the 1125x2436 frame to the 1080x2340 panel (0296, sweetfb_fit). So the clock sat on the hole.
 *
 * The kernel inserts this library into every program (kern_exec.c 0297: only when the file exists; boot-arg
 * sweetsb=0 = never). In a program without UIKit the class is not there and nothing happens. In UIKit programs
 * +[_UIStatusBarVisualProvider_Split1125 leadingCenteringOffset] gets SWEET_SB_DX points added (default below;
 * the first number in /usr/local/etc/sweet-sb.txt overrides it). Its superclass FixedSplit derives the trailing
 * inset (battery, Wi-Fi, signal) and the pill's from the leading one: -trailingCenteringEdgeInset =
 * -leadingCenteringEdgeInset + [class trailingCenteringOffset], -pillCenteringEdgeInset = leading + [class
 * pillCenteringOffset]. So the pill follows the clock by itself, and Split1125 gets its own
 * -trailingCenteringEdgeInset (FixedSplit's minus SWEET_SB_DX) to keep the right side where it was. Not
 * +trailingCenteringOffset: Control Center's expanded bar puts its carrier name at expandedEdgeInset +
 * trailingCenteringOffset from the left (-[FixedSplit expandedEdgeInsets]), so that moved the carrier 44 pt left. Nothing on the
 * root changes; only in-memory methods are replaced / added.
 *
 * Keyboard: the Mi 10's long sides curve, and the iPhone X keyboard runs edge to edge, so the outer keys (Q, P,
 * A, L, shift, delete) sat on the curve: hard to hit, uncomfortable. The keys are kept SWEET_KB_INSET pt in from
 * each side (second number in /usr/local/etc/sweet-sb.txt; 0 = off), in one of three ways (third number, mode):
 *   0  horizontal scale on UIKeyboardLayoutStar (the view that draws and hit-tests the keys): placement right,
 *      but P / L drew wrong (the view's frame, now narrower than its keys, seems to cut them);
 *   1  narrower frames for UIKeyboardLayoutStar: the keyboard ended up partly off the screen;
 *   2  (default) the same scale on the star's superview (UIKeyboardImpl, inside UIKeyboardAutomatic), so the
 *      star keeps its full 375 pt frame: placement right, background full width (user, 2026-11-14); P looks
 *      ~2 px narrower, resampling (0.936 here and the panel's 0.96), nothing in the chain clips.
 * A frame set on a scaled view would resize its bounds, so setFrame: clears the scale, sets the frame, scales
 * again. Touches go through the transform (UIKit). The first layout in each program writes the keyboard's view
 * chain to /var/tmp/sweet-kb-PROGRAM.txt (class, frame, bounds, scale, clips) for the next step.
 */
#include <objc/runtime.h>
#include <objc/message.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#define SWEET_SB_DX 44.0            /* points: clock centre from ~41 pt to ~85 pt, clear of the hole (to 57 pt) */
#define SWEET_KB_INSET 12.0         /* points kept free of keys on each side of the keyboard */

typedef struct { double x, y, w, h; } sweet_rect;

typedef struct { double a, b, c, d, tx, ty; } sweet_affine;

static double sweet_sb_dx = SWEET_SB_DX, sweet_kb_inset = SWEET_KB_INSET;
static int sweet_kb_mode = 2;
static IMP sweet_sb_orig_leading, sweet_sb_orig_trailing;
static IMP sweet_kb_orig_layout, sweet_kb_orig_setframe, sweet_kb_orig_sv_setframe;
static Class sweet_kb_sv_class;
static int sweet_kb_dumped;

static sweet_rect
sweet_rect_of(id v, const char *name)
{
	return ((sweet_rect (*)(id, SEL))objc_msgSend)(v, sel_registerName(name));
}

static id
sweet_superview(id v)
{
	return ((id (*)(id, SEL))objc_msgSend)(v, sel_registerName("superview"));
}

static void
sweet_kb_set_transform(id v, double sx)
{
	sweet_affine t = ((sweet_affine (*)(id, SEL))objc_msgSend)(v, sel_registerName("transform"));
	if (t.a != sx || t.b != 0 || t.c != 0 || t.d != 1 || t.tx != 0 || t.ty != 0) {
		sweet_affine n = { sx, 0, 0, 1, 0, 0 };
		((void (*)(id, SEL, sweet_affine))objc_msgSend)(v, sel_registerName("setTransform:"), n);
	}
}

/* the scale that keeps sweet_kb_inset free on each side of v's (untransformed) width; 1 when too narrow */
static double
sweet_kb_scale(id v)
{
	sweet_rect b = sweet_rect_of(v, "bounds");
	if (b.w < 300 || b.w < 4 * sweet_kb_inset) {
		return 1;
	}
	return (b.w - 2 * sweet_kb_inset) / b.w;
}

static void
sweet_kb_dump(id star)
{
	char path[256];
	snprintf(path, sizeof(path), "/var/tmp/sweet-kb-%s.txt", getprogname());
	FILE *f = fopen(path, "w");
	if (f == NULL) {
		return;
	}
	fprintf(f, "mode %d inset %.1f\n", sweet_kb_mode, sweet_kb_inset);
	for (id v = star; v != nil; v = sweet_superview(v)) {
		sweet_rect fr = sweet_rect_of(v, "frame"), b = sweet_rect_of(v, "bounds");
		sweet_affine t = ((sweet_affine (*)(id, SEL))objc_msgSend)(v, sel_registerName("transform"));
		BOOL clips = ((BOOL (*)(id, SEL))objc_msgSend)(v, sel_registerName("clipsToBounds"));
		fprintf(f, "%s frame %.1f %.1f %.1f %.1f bounds %.1f %.1f %.1f %.1f scale %.4f clips %d\n",
		    object_getClassName(v), fr.x, fr.y, fr.w, fr.h, b.x, b.y, b.w, b.h, t.a, (int)clips);
	}
	id subs = ((id (*)(id, SEL))objc_msgSend)(star, sel_registerName("subviews"));
	unsigned long n = ((unsigned long (*)(id, SEL))objc_msgSend)(subs, sel_registerName("count"));
	for (unsigned long i = 0; i < n && i < 16; i++) {
		id v = ((id (*)(id, SEL, unsigned long))objc_msgSend)(subs, sel_registerName("objectAtIndex:"), i);
		sweet_rect fr = sweet_rect_of(v, "frame");
		fprintf(f, "  sub %s frame %.1f %.1f %.1f %.1f\n", object_getClassName(v), fr.x, fr.y, fr.w, fr.h);
	}
	fclose(f);
}

/* mode 0: the star itself; mode 1: its frames; mode 2: its superview */
static void
sweet_kb_setframe(id self, SEL cmd, sweet_rect r)
{
	if (sweet_kb_mode == 1) {
		if (r.w > 300 && r.x > -1 && r.x < 1) {
			r.x += sweet_kb_inset;
			r.w -= 2 * sweet_kb_inset;
		}
		((void (*)(id, SEL, sweet_rect))sweet_kb_orig_setframe)(self, cmd, r);
		return;
	}
	if (sweet_kb_mode == 0) {
		sweet_kb_set_transform(self, 1);
	}
	((void (*)(id, SEL, sweet_rect))sweet_kb_orig_setframe)(self, cmd, r);
	if (sweet_kb_mode == 0) {
		sweet_kb_set_transform(self, sweet_kb_scale(self));
	}
}

static void
sweet_kb_sv_setframe(id self, SEL cmd, sweet_rect r)
{
	sweet_affine t = ((sweet_affine (*)(id, SEL))objc_msgSend)(self, sel_registerName("transform"));
	bool scaled = t.a != 1;
	if (scaled) {
		sweet_kb_set_transform(self, 1);
	}
	((void (*)(id, SEL, sweet_rect))sweet_kb_orig_sv_setframe)(self, cmd, r);
	if (scaled) {
		sweet_kb_set_transform(self, sweet_kb_scale(self));
	}
}

static void
sweet_kb_layout(id self, SEL cmd)
{
	((void (*)(id, SEL))sweet_kb_orig_layout)(self, cmd);
	if (sweet_kb_mode == 0) {
		sweet_kb_set_transform(self, sweet_kb_scale(self));
	} else if (sweet_kb_mode == 2) {
		id sv = sweet_superview(self);
		const char *cn = sv != nil ? object_getClassName(sv) : "";
		/* only a keyboard class gets the setFrame: hook (never plain UIView) */
		if (sv != nil && (strncmp(cn, "UIKeyboard", 10) == 0 || strncmp(cn, "_UIKB", 5) == 0)) {
			Class svc = object_getClass(sv);
			if (sweet_kb_sv_class == Nil) {
				SEL fsel = sel_registerName("setFrame:");
				Method m = class_getInstanceMethod(svc, fsel);
				if (m != NULL) {
					sweet_kb_orig_sv_setframe = method_getImplementation(m);
					if (!class_addMethod(svc, fsel, (IMP)sweet_kb_sv_setframe, method_getTypeEncoding(m))) {
						sweet_kb_orig_sv_setframe = method_setImplementation(m, (IMP)sweet_kb_sv_setframe);
					}
					sweet_kb_sv_class = svc;
				}
			}
			if (svc == sweet_kb_sv_class) {
				sweet_kb_set_transform(sv, sweet_kb_scale(sv));
			}
		}
	}
	if (!sweet_kb_dumped) {
		sweet_kb_dumped = 1;
		sweet_kb_dump(self);
	}
}

static double
sweet_sb_leading(id self, SEL cmd)
{
	return ((double (*)(id, SEL))sweet_sb_orig_leading)(self, cmd) + sweet_sb_dx;
}

static double
sweet_sb_trailing(id self, SEL cmd)
{
	return ((double (*)(id, SEL))sweet_sb_orig_trailing)(self, cmd) - sweet_sb_dx;
}

static IMP
sweet_sb_hook(Class c, const char *name, IMP imp)
{
	Method m = class_getClassMethod(c, sel_registerName(name));
	return m != NULL ? method_setImplementation(m, imp) : NULL;
}

__attribute__((constructor))
static void
sweet_sb_init(void)
{
	Class c = objc_getClass("_UIStatusBarVisualProvider_Split1125");
	if (c == Nil) {
		return;                     /* no UIKit in this program */
	}
	FILE *f = fopen("/usr/local/etc/sweet-sb.txt", "r");
	if (f != NULL) {
		double dx, kb;
		int mode;
		if (fscanf(f, "%lf", &dx) == 1 && dx > -200 && dx < 200) {
			sweet_sb_dx = dx;
			if (fscanf(f, "%lf", &kb) == 1 && kb >= 0 && kb < 60) {
				sweet_kb_inset = kb;
				if (fscanf(f, "%d", &mode) == 1 && mode >= 0 && mode <= 2) {
					sweet_kb_mode = mode;
				}
			}
		}
		fclose(f);
	}
	sweet_sb_orig_leading = sweet_sb_hook(c, "leadingCenteringOffset", (IMP)sweet_sb_leading);
	/* -trailingCenteringEdgeInset is FixedSplit's (inherited): add Split1125's own instead of replacing it */
	SEL tsel = sel_registerName("trailingCenteringEdgeInset");
	Method tm = class_getInstanceMethod(c, tsel);
	if (tm != NULL) {
		sweet_sb_orig_trailing = method_getImplementation(tm);
		if (!class_addMethod(c, tsel, (IMP)sweet_sb_trailing, method_getTypeEncoding(tm))) {
			sweet_sb_orig_trailing = method_setImplementation(tm, (IMP)sweet_sb_trailing);
		}
	}
	Class kbc = objc_getClass("UIKeyboardLayoutStar");
	SEL fsel = sel_registerName("setFrame:");
	Method fm = kbc != Nil && sweet_kb_inset > 0 ? class_getInstanceMethod(kbc, fsel) : NULL;
	Method lm = fm != NULL ? class_getInstanceMethod(kbc, sel_registerName("layoutSubviews")) : NULL;
	if (fm != NULL && lm != NULL) {
		sweet_kb_orig_setframe = method_getImplementation(fm);
		if (!class_addMethod(kbc, fsel, (IMP)sweet_kb_setframe, method_getTypeEncoding(fm))) {
			sweet_kb_orig_setframe = method_setImplementation(fm, (IMP)sweet_kb_setframe);
		}
		sweet_kb_orig_layout = method_setImplementation(lm, (IMP)sweet_kb_layout);
	}
	if (getenv("SWEET_SB_LOG") != NULL) {
		fprintf(stderr, "sweet-sb: dx %.1f pt, leading %s, trailing %s\n", sweet_sb_dx,
		    sweet_sb_orig_leading ? "hooked" : "missing", sweet_sb_orig_trailing ? "hooked" : "missing");
	}
}
