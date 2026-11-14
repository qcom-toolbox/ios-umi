/*
 * sweet-oslog-dump SECONDS [PROCESS...]: stream the live unified log for SECONDS (default 120) through
 * LoggingSupport's private live stream (what oslog / the syslog relay use) and print every entry of the named
 * processes (all when none): time, process[pid], level, subsystem, category, message. The ad-hoc signature carries
 * the private logging entitlements (phone/heic-trace/oslog.entitlements).
 */
#import <Foundation/Foundation.h>
#import <objc/message.h>

int
main(int argc, char **argv)
{
	@autoreleasepool {
		double secs = argc > 1 ? atof(argv[1]) : 120;
		NSMutableSet *names = [NSMutableSet set];
		for (int i = 2; i < argc; i++)
			[names addObject:@(argv[i])];
		[[NSBundle bundleWithPath:@"/System/Library/PrivateFrameworks/LoggingSupport.framework"] load];
		Class storeClass = NSClassFromString(@"OSLogEventLiveStore"), streamClass = NSClassFromString(@"OSLogEventLiveStream");
		if (storeClass == nil || streamClass == nil) {
			fprintf(stderr, "LoggingSupport classes missing\n");
			return 1;
		}
		id store = ((id(*)(id, SEL))objc_msgSend)(storeClass, NSSelectorFromString(@"liveLocalStore"));
		__block unsigned long n = 0;
		dispatch_semaphore_t ready = dispatch_semaphore_create(0);
		__block id stream = nil;
		void (^prepared)(id, NSError *) = ^(id source, NSError *error) {
			if (source == nil) {
				fprintf(stderr, "prepare: %s\n", error.description.UTF8String);
				dispatch_semaphore_signal(ready);
				return;
			}
			stream = ((id(*)(id, SEL, id))objc_msgSend)([streamClass alloc], NSSelectorFromString(@"initWithLiveSource:"), source);
			((void (*)(id, SEL, unsigned long))objc_msgSend)(stream, NSSelectorFromString(@"setFlags:"), 0x1 | 0x2 | 0x4);
			void (^handler)(id) = ^(id ev) {
				NSString *proc = [ev valueForKey:@"process"];
				if (names.count && (proc == nil || ![names containsObject:proc]))
					return;
				NSString *msg = [ev valueForKey:@"composedMessage"];
				printf("%s %s[%d] %u %s %s: %s\n", [[ev valueForKey:@"date"] description].UTF8String, proc.UTF8String,
				    [[ev valueForKey:@"processIdentifier"] intValue], [[ev valueForKey:@"logType"] unsignedIntValue],
				    [[ev valueForKey:@"subsystem"] UTF8String] ?: "", [[ev valueForKey:@"category"] UTF8String] ?: "",
				    msg.UTF8String ?: "");
				fflush(stdout);
				n++;
			};
			((void (*)(id, SEL, id))objc_msgSend)(stream, NSSelectorFromString(@"setEventHandler:"), handler);
			((void (*)(id, SEL))objc_msgSend)(stream, NSSelectorFromString(@"activate"));
			fprintf(stderr, "streaming for %.0f s\n", secs);
			dispatch_semaphore_signal(ready);
		};
		((void (*)(id, SEL, id))objc_msgSend)(store, NSSelectorFromString(@"prepareWithCompletionHandler:"), prepared);
		dispatch_semaphore_wait(ready, dispatch_time(DISPATCH_TIME_NOW, 10 * NSEC_PER_SEC));
		[[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:secs]];
		fprintf(stderr, "%lu entries\n", n);
	}
	return 0;
}
