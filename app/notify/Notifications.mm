#include "Notifications.hpp"

#import <Foundation/Foundation.h>
#import <UserNotifications/UserNotifications.h>
#include <objc/runtime.h>

#include <QUuid>

#include <utility>

/// Shows Kiebitz's notifications as banners also while Kiebitz is the app in front.
@interface KiebitzNotificationDelegate : NSObject <UNUserNotificationCenterDelegate>
@end

@implementation KiebitzNotificationDelegate
- (void)userNotificationCenter:(UNUserNotificationCenter*)center
       willPresentNotification:(UNNotification*)notification
         withCompletionHandler:(void (^)(UNNotificationPresentationOptions))completionHandler {
    (void)center;
    (void)notification;
    completionHandler(UNNotificationPresentationOptionBanner | UNNotificationPresentationOptionList);
}
@end

namespace kiebitz::app::notifications {
namespace {

/// A class, to send it a message. (Messages to a class written as such compile, with Homebrew's
/// clang, to stubs the system linker doesn't know; through the runtime they are plain calls.)
id classNamed(const char* name) {
    return (id)objc_getClass(name); // NOLINT
}

std::function<void(const QString&, const QString&)>& sink() {
    static std::function<void(const QString&, const QString&)> function;
    return function;
}

/// The notification centre, or nil outside an app bundle (it would throw there).
UNUserNotificationCenter* center() {
    NSBundle* bundle = [classNamed("NSBundle") mainBundle];
    if ([bundle bundleIdentifier] == nil || ![[bundle bundlePath] hasSuffix:@".app"]) {
        return nil;
    }
    UNUserNotificationCenter* result =
        [classNamed("UNUserNotificationCenter") currentNotificationCenter];
    static KiebitzNotificationDelegate* delegate = [[KiebitzNotificationDelegate alloc] init];
    if (result.delegate == nil) {
        result.delegate = delegate;
    }
    return result;
}


} // namespace

void requestPermission() {
    if (sink()) {
        return;
    }
    UNUserNotificationCenter* notifications = center();
    if (notifications == nil) {
        return;
    }
    // No sound of its own: Kiebitz plays one when it posts (also without permission).
    [notifications requestAuthorizationWithOptions:UNAuthorizationOptionAlert
                                 completionHandler:^(BOOL granted, NSError* error) {
                                   (void)granted;
                                   (void)error;
                                 }];
}

void post(const QString& title, const QString& body) {
    if (sink()) {
        sink()(title, body);
        return;
    }
    UNUserNotificationCenter* notifications = center();
    if (notifications == nil) {
        return;
    }
    UNMutableNotificationContent* content = [[UNMutableNotificationContent alloc] init];
    content.title = title.toNSString();
    content.body = body.toNSString();
    UNNotificationRequest* request = [classNamed("UNNotificationRequest")
        requestWithIdentifier:QUuid::createUuid().toString(QUuid::WithoutBraces).toNSString()
                      content:content
                      trigger:nil];
    [notifications addNotificationRequest:request withCompletionHandler:nil];
}

void setSinkForTests(std::function<void(const QString& title, const QString& body)> function) {
    sink() = std::move(function);
}

} // namespace kiebitz::app::notifications
