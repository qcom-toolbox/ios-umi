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
static CGRect (*originalPortrait)(id, SEL, CGRect, NSDirectionalEdgeInsets, NSInteger);
static void (*originalLayout)(id, SEL);
static unsigned layouts;
static const CGFloat cameraX = 133.0 / 2.88, cameraY = 58.0 / 2.88;

static long artwork(void) { return 2556; }
static bool enabled(void) { return true; }
static CGSize sensorSize(id self, SEL cmd) { (void)self; (void)cmd; return CGSizeMake(24, 24); }
static CGSize inertSize(id self, SEL cmd) { (void)self; (void)cmd; return CGSizeMake(28, 28); }
/* There is one camera, no visible space between two sensors. */
static CGRect interSensor(id self, SEL cmd) { (void)self; (void)cmd; return CGRectZero; }

static CGRect sensorFrame(id self, SEL cmd, CGRect bounds)
{
    CGRect r = originalSensor(self, cmd, bounds);
    /* Landscape keeps Apple's orientation handling until measured on hardware. */
    if (bounds.size.height >= bounds.size.width) {
        r.origin.x = bounds.origin.x + cameraX - r.size.width / 2;
        r.origin.y = bounds.origin.y + cameraY - r.size.height / 2;
    }
    return r;
}

static CGRect portraitFrame(id self, SEL cmd, CGRect bounds,
                            NSDirectionalEdgeInsets outsets, NSInteger direction)
{
    CGRect r = originalPortrait(self, cmd, bounds, outsets, direction);
    /* Expanded cards grow to the right of the hole instead of extending off-screen. */
    CGFloat left = CGRectGetMinX(bounds) + 6;
    CGFloat right = CGRectGetMaxX(bounds) - 6;
    r.size.width = MIN(r.size.width, MAX(0, right - left));
    r.origin.x = MAX(left, MIN(r.origin.x, right - r.size.width));
    r.origin.y = MAX(CGRectGetMinY(bounds) + 4, r.origin.y);
    return r;
}

static void layout(id self, SEL cmd)
{
    originalLayout(self, cmd);
    if (layouts++ < 12 && logfile) {
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
        "viewDidLayoutSubviews", "minimumSensorRegionFrame"};
    for (unsigned i = 0; i < sizeof(metricMethods)/sizeof(*metricMethods); i++)
        if (!class_getInstanceMethod(metrics, sel_registerName(metricMethods[i]))) goto missing;
    for (unsigned i = 0; i < sizeof(controllerMethods)/sizeof(*controllerMethods); i++)
        if (!class_getInstanceMethod(controller, sel_registerName(controllerMethods[i]))) goto missing;
    void *substrate = dlopen("/usr/lib/libsubstrate.dylib", RTLD_NOW);
    void (*hookFunction)(void *, void *, void **) = substrate ? dlsym(substrate, "MSHookFunction") : NULL;
    long (*subtype)(void) = dlsym(RTLD_DEFAULT, "SBFEffectiveArtworkSubtype");
    bool (*jindo)(void) = dlsym(RTLD_DEFAULT, "SBSEnableJindo");
    if (!hookFunction || !subtype || !jindo) goto missing;
    if (logfile) { fprintf(logfile, "before: artwork %ld enabled %d\n", subtype(), jindo()); fflush(logfile); }
    hook(metrics, metricMethods[0], (IMP)sensorSize);
    hook(metrics, metricMethods[1], (IMP)inertSize);
    hook(metrics, metricMethods[2], (IMP)interSensor);
    originalSensor = (void *)hook(controller, controllerMethods[0], (IMP)sensorFrame);
    originalPortrait = (void *)hook(controller, controllerMethods[1], (IMP)portraitFrame);
    originalLayout = (void *)hook(controller, controllerMethods[2], (IMP)layout);
    hookFunction((void *)subtype, (void *)artwork, NULL);
    hookFunction((void *)jindo, (void *)enabled, NULL);
    if (logfile) { fprintf(logfile, "installed: artwork %ld enabled %d camera %.3f %.3f\n",
        subtype(), jindo(), cameraX, cameraY); fflush(logfile); }
    return;
missing:
    if (logfile) { fprintf(logfile, "not installed: required methods or hook symbols missing\n"); fclose(logfile); logfile = NULL; }
}

__attribute__((constructor)) static void prepare(void)
{
    if (strcmp(getprogname(), "SpringBoard") || access("/var/tmp/sweet-island.disabled", F_OK) == 0) return;
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
    });
}
