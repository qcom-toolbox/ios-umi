/* Native iOS 16 System Aperture on umi. Loaded only in SpringBoard by libsweet-sb.
 * Panel camera: (133,58), radius 31 px; compositor maps 375x812 points to 1080x2340.
 * No disk MobileGestalt changes: artwork selection is overridden only in this process.
 */
#import <UIKit/UIKit.h>
#import <objc/runtime.h>
#import <objc/message.h>
#include <dlfcn.h>
#include <stdio.h>
#include <unistd.h>

static FILE *logfile;
static CGRect (*originalSensor)(id, SEL, CGRect);
static CGRect (*originalMinimumSensor)(id, SEL);
static CGRect (*originalPortrait)(id, SEL, CGRect, NSDirectionalEdgeInsets, NSInteger);
static void (*originalLayout)(id, SEL);
static unsigned layouts;
static const CGFloat cameraX = 133.0 / 2.88, cameraY = 58.0 / 2.88;

static void dumpView(FILE *f, UIView *view, unsigned depth, unsigned *count)
{
    if (depth > 12 || (*count)++ > 600) return;
    CGRect r = [view convertRect:view.bounds toView:nil];
    fprintf(f, "%*s%s %.1f %.1f %.1f %.1f hidden %d alpha %.2f\n", depth,
        "", object_getClassName(view), r.origin.x, r.origin.y, r.size.width, r.size.height,
        view.hidden, view.alpha);
    for (UIView *child in view.subviews) dumpView(f, child, depth + 1, count);
}

static void dumpWindows(void)
{
    FILE *f = fopen("/var/tmp/sweet-island-views.log", "w");
    if (!f) return;
    unsigned count = 0;
    for (UIScene *scene in UIApplication.sharedApplication.connectedScenes) {
        if (![scene isKindOfClass:UIWindowScene.class]) continue;
        for (UIWindow *window in ((UIWindowScene *)scene).windows) dumpView(f, window, 0, &count);
    }
    fclose(f);
}

/* 20H392's SBFEffectiveArtworkSubtype caches its result in writable __DATA.
 * Decode the guarded ADRP x8 / LDR x0 pair after dispatch_once's fast-path
 * check. This changes per-process data, not signed executable instructions.
 * Fail closed on a different implementation; never guess an absolute address.
 */
static long *artworkCache(long (*subtype)(void))
{
    const uint32_t *code = (const void *)subtype;
    if ((code[6] & 0x9f00001f) != 0x90000008 ||
        (code[7] & 0xffc003ff) != 0xf9400100 ||
        code[8] != 0xa8c17bfd || code[9] != 0xd65f03c0) return NULL;
    int64_t pages = ((code[6] >> 29) & 3) | (((code[6] >> 5) & 0x7ffff) << 2);
    if (pages & (1 << 20)) pages -= 1 << 21;
    uintptr_t page = (uintptr_t)&code[6] & ~(uintptr_t)0xfff;
    long *value = (void *)(page + pages * 4096 + ((code[7] >> 10) & 0xfff) * 8);
    return *value == subtype() ? value : NULL;
}
/* Activity renderers (ClockAngel) ask SpringBoardServices' SBSIsSystemApertureAvailable, which reads the
 * MobileGestalt artwork subtype (2436 here) once and caches YES only for 2556/2796. With NO, the client's
 * SBUISystemApertureElementSource never publishes layout modes >= 2, so SpringBoard's
 * SBSystemApertureSceneElement never becomes ready and the Live Activity is not shown in the island.
 * Same fast path as above: ADRP x8 / LDRB w0 after the dispatch_once check; fail closed otherwise. */
static uint8_t *apertureCache(bool (*available)(void))
{
    const uint32_t *code = (const void *)available;
    if ((code[6] & 0x9f00001f) != 0x90000008 ||
        (code[7] & 0xffc003ff) != 0x39400100 ||
        code[8] != 0xa8c17bfd || code[9] != 0xd65f03c0) return NULL;
    int64_t pages = ((code[6] >> 29) & 3) | (((code[6] >> 5) & 0x7ffff) << 2);
    if (pages & (1 << 20)) pages -= 1 << 21;
    uintptr_t page = (uintptr_t)&code[6] & ~(uintptr_t)0xfff;
    uint8_t *value = (void *)(page + pages * 4096 + ((code[7] >> 10) & 0xfff));
    return *value == (uint8_t)available() ? value : NULL;
}

static void installClient(void)
{
    FILE *f = fopen("/var/tmp/sweet-island-client.log", "a");
    bool (*available)(void) = dlsym(RTLD_DEFAULT, "SBSIsSystemApertureAvailable");
    bool before = available ? available() : false; /* initialize the dispatch_once cache */
    uint8_t *cached = available ? apertureCache(available) : NULL;
    if (cached) *cached = 1;
    if (f) {
        fprintf(f, "%s[%d]: aperture available %d -> %d%s\n", getprogname(), getpid(), before,
                available ? available() : -1, cached ? "" : " (not patched: pattern mismatch)");
        fclose(f);
    }
}

static CGSize sensorSize(id self, SEL cmd) { (void)self; (void)cmd; return CGSizeMake(24, 24); }
static CGSize inertSize(id self, SEL cmd) { (void)self; (void)cmd; return CGSizeMake(28, 28); }
static BOOL hideFallback(id self, SEL cmd) { (void)self; (void)cmd; return YES; }
/* There is one camera, no visible space between two sensors. */
static CGRect interSensor(id self, SEL cmd) { (void)self; (void)cmd; return CGRectZero; }

/* The aperture lives in the hardware's (portrait) space on every Dynamic Island iPhone: in landscape SpringBoard
 * keeps the pill at the camera and rotates the element content (transformForRotationToInterfaceOrientation:; there
 * is only a _portraitFrameForPrimaryContainerView...). So the camera position applies in every orientation, unless
 * the bounds turn out to be landscape-shaped (then logged and left to Apple; see sweet-island.log). */
static unsigned rotatedLogs;
static void logRotated(const char *what, id self, CGRect bounds, CGRect original, CGRect result)
{
    NSInteger orientation = ((NSInteger (*)(id, SEL))objc_msgSend)(self,
        sel_registerName("_activeElementInterfaceOrientation"));
    if (orientation == UIInterfaceOrientationPortrait || orientation == UIInterfaceOrientationUnknown) return;
    if (!logfile || rotatedLogs++ >= 80) return;
    fprintf(logfile, "%s orientation %ld bounds %.1f %.1f %.1f %.1f apple %.1f %.1f %.1f %.1f -> %.1f %.1f\n", what,
            (long)orientation, bounds.origin.x, bounds.origin.y, bounds.size.width, bounds.size.height,
            original.origin.x, original.origin.y, original.size.width, original.size.height,
            result.origin.x, result.origin.y);
    fflush(logfile);
}

static CGRect sensorFrame(id self, SEL cmd, CGRect bounds)
{
    CGRect original = originalSensor(self, cmd, bounds), r = original;
    /* Bounds can be a short status-bar strip; only a landscape-shaped area (wider than the 375 pt panel) is
     * rotated interface space. */
    if (bounds.size.width <= 376) {
        r.origin.x = bounds.origin.x + cameraX - r.size.width / 2;
        r.origin.y = bounds.origin.y + cameraY - r.size.height / 2;
    }
    logRotated("sensor", self, bounds, original, r);
    return r;
}

static CGRect minimumSensorFrame(id self, SEL cmd)
{
    CGRect original = originalMinimumSensor(self, cmd), r = original;
    r.origin.x = cameraX - r.size.width / 2;
    r.origin.y = cameraY - r.size.height / 2;
    logRotated("minimum", self, CGRectZero, original, r);
    return r;
}

static CGRect portraitFrame(id self, SEL cmd, CGRect bounds,
                            NSDirectionalEdgeInsets outsets, NSInteger direction)
{
    CGRect original = originalPortrait(self, cmd, bounds, outsets, direction), r = original;
    /* Expanded cards grow to the right of the hole instead of extending off-screen. */
    CGFloat left = CGRectGetMinX(bounds) + 6;
    CGFloat right = CGRectGetMaxX(bounds) - 6;
    r.size.width = MIN(r.size.width, MAX(0, right - left));
    r.origin.x = MAX(left, MIN(r.origin.x, right - r.size.width));
    r.origin.y = MAX(CGRectGetMinY(bounds) + 4, r.origin.y);
    logRotated("expanded", self, bounds, original, r);
    return r;
}

static void layout(id self, SEL cmd)
{
    originalLayout(self, cmd);
    if (layouts++ < 100 && logfile) {
        UIView *view = ((id (*)(id, SEL))objc_msgSend)(self, sel_registerName("view"));
        CGRect sensor = ((CGRect (*)(id, SEL))objc_msgSend)(self, sel_registerName("minimumSensorRegionFrame"));
        fprintf(logfile, "layout %u view %.1f %.1f sensor %.1f %.1f %.1f %.1f\n",
                layouts, view.bounds.size.width, view.bounds.size.height,
                sensor.origin.x, sensor.origin.y, sensor.size.width, sensor.size.height);
        fflush(logfile);
    }
}

static IMP hook(Class cls, const char *name, IMP replacement)
{
    SEL sel = sel_registerName(name);
    Method m = class_getInstanceMethod(cls, sel);
    IMP old = method_getImplementation(m);
    if (!class_addMethod(cls, sel, replacement, method_getTypeEncoding(m)))
        method_setImplementation(m, replacement);
    return old;
}

static void install(void)
{
    if (strcmp(getprogname(), "SpringBoard") || access("/var/tmp/sweet-island.disabled", F_OK) == 0) return;
    logfile = fopen("/var/tmp/sweet-island.log", "w");
    Class metrics = objc_getClass("SBUISystemApertureLayoutMetrics");
    Class controller = objc_getClass("SBSystemApertureViewController");
    const char *metricMethods[] = {"sensorRegionSize", "_inertSize", "interSensorRegionInWindowSpace"};
    const char *controllerMethods[] = {"_frameForSensorRegionInBounds:",
        "_portraitFrameForPrimaryContainerViewInBounds:withPreferredEdgeOutsets:layoutDirection:",
        "viewDidLayoutSubviews", "minimumSensorRegionFrame", "_activeElementInterfaceOrientation"};
    for (unsigned i = 0; i < sizeof(metricMethods)/sizeof(*metricMethods); i++)
        if (!class_getInstanceMethod(metrics, sel_registerName(metricMethods[i]))) goto missing;
    for (unsigned i = 0; i < sizeof(controllerMethods)/sizeof(*controllerMethods); i++)
        if (!class_getInstanceMethod(controller, sel_registerName(controllerMethods[i]))) goto missing;
    long (*subtype)(void) = dlsym(RTLD_DEFAULT, "SBFEffectiveArtworkSubtype");
    bool (*jindo)(void) = dlsym(RTLD_DEFAULT, "SBSEnableJindo");
    if (!subtype || !jindo || !jindo()) goto missing;
    long oldSubtype = subtype(); /* initialize the dispatch_once cache */
    long *cachedSubtype = artworkCache(subtype);
    if (oldSubtype != 2436 || !cachedSubtype) goto missing;
    if (logfile) { fprintf(logfile, "before: artwork %ld enabled %d\n", subtype(), jindo()); fflush(logfile); }
    hook(metrics, metricMethods[0], (IMP)sensorSize);
    hook(metrics, metricMethods[1], (IMP)inertSize);
    hook(metrics, metricMethods[2], (IMP)interSensor);
    originalSensor = (void *)hook(controller, controllerMethods[0], (IMP)sensorFrame);
    originalPortrait = (void *)hook(controller, controllerMethods[1], (IMP)portraitFrame);
    originalLayout = (void *)hook(controller, controllerMethods[2], (IMP)layout);
    originalMinimumSensor = (void *)hook(controller, controllerMethods[3], (IMP)minimumSensorFrame);
    Class settings = objc_getClass("SBSystemApertureSettings");
    if (class_getInstanceMethod(settings, sel_registerName("hideRemotePrototypingFallbackPill")))
        hook(settings, "hideRemotePrototypingFallbackPill", (IMP)hideFallback);
    *cachedSubtype = 2556;
    if (logfile) { fprintf(logfile, "installed: artwork %ld enabled %d camera %.3f %.3f\n",
        subtype(), jindo(), cameraX, cameraY); fflush(logfile); }
    return;
missing:
    if (logfile) { fprintf(logfile, "not installed: required methods or hook symbols missing\n"); fclose(logfile); logfile = NULL; }
}

__attribute__((constructor)) static void prepare(void)
{
    if (access("/var/tmp/sweet-island.disabled", F_OK) == 0) return;
    if (strcmp(getprogname(), "ClockAngel") == 0) { installClient(); return; }
    if (strcmp(getprogname(), "SpringBoard")) return;
    /* One failed launch automatically restores the ordinary UI on the next launch. */
    if (access("/var/tmp/sweet-island.starting", F_OK) == 0) {
        FILE *f = fopen("/var/tmp/sweet-island.disabled", "w");
        if (f) fclose(f);
        return;
    }
    FILE *f = fopen("/var/tmp/sweet-island.starting", "w");
    if (f) fclose(f);
    install();
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 20 * NSEC_PER_SEC), dispatch_get_main_queue(), ^{
        unlink("/var/tmp/sweet-island.starting");
        if (logfile) {
            fprintf(logfile, "main loop alive, layouts %u\n", layouts);
            fflush(logfile);
        }
        dumpWindows();
    });
}
