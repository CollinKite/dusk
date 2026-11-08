#include "dusk/duo_native.hpp"
#include <SDL3/SDL.h>
#import <UIKit/UIKit.h>
#include <mutex>

namespace {
std::mutex s_metricsMutex;
dusk::duo::Metrics s_metrics;
}

// A transparent, noninteractive observer stays with SDL's existing view/scene.
// UIKit geometry and hinge callbacks never read or mutate the game's settings.
@interface DuskDuoObserver : UIView
@property(nonatomic) BOOL hingeAvailable;
@property(nonatomic) BOOL hingeClosed;
@property(nonatomic) BOOL hingePartial;
- (void)publishMetrics;
@end

@implementation DuskDuoObserver
- (instancetype)initWithFrame:(CGRect)frame {
    self = [super initWithFrame:frame];
    if (self) {
        self.userInteractionEnabled = NO;
        self.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
        self.backgroundColor = UIColor.clearColor;
#if __IPHONE_OS_VERSION_MAX_ALLOWED >= 270100
        if (@available(iOS 27.1, *)) {
            __weak DuskDuoObserver* weakSelf = self;
            UIHingeInteraction* interaction = [[UIHingeInteraction alloc]
                initWithUpdateHandler:^(UIHingeInteraction*, UIHingeInteractionUpdate* update) {
                    DuskDuoObserver* observer = weakSelf;
                    if (!observer) return;
                    observer.hingeAvailable = update.hinge != nil;
                    observer.hingeClosed = update.hinge.status == UIHingeStatusClosed;
                    observer.hingePartial = update.hinge.status == UIHingeStatusPartiallyOpen;
                    [observer publishMetrics];
                }];
            [self addInteraction:interaction];
        }
#endif
    }
    return self;
}
- (void)layoutSubviews { [super layoutSubviews]; [self publishMetrics]; }
- (void)safeAreaInsetsDidChange { [super safeAreaInsetsDidChange]; [self publishMetrics]; }
- (void)didMoveToWindow { [super didMoveToWindow]; [self publishMetrics]; }
- (void)publishMetrics {
    dusk::duo::Metrics metrics;
    if (self.window) {
        metrics.width = self.bounds.size.width;
        metrics.height = self.bounds.size.height;
        metrics.left = self.safeAreaInsets.left;
        metrics.right = self.safeAreaInsets.right;
        metrics.bottom = self.safeAreaInsets.bottom;
        metrics.hingeAvailable = self.hingeAvailable;
        metrics.closed = self.hingeClosed;
        metrics.partiallyOpen = self.hingePartial;
#if __IPHONE_OS_VERSION_MAX_ALLOWED >= 270100
        if (@available(iOS 27.1, *)) {
            for (UIViewReservedRegion* region in [self reservedRegionsOfKind:
                    UIViewReservedRegionKind.divisionRegionKind
                    options:UIViewReservedRegionQueryOptionsIncludeInactive]) {
                const CGRect rect = CGRectIntersection(self.bounds, region.frame);
                if (!CGRectIsNull(rect) && rect.size.width > metrics.width * .5f &&
                    rect.size.width > rect.size.height) {
                    metrics.divisionTop = CGRectGetMinY(rect);
                    metrics.divisionBottom = CGRectGetMaxY(rect);
                    break;
                }
            }
        }
#endif
    }
    std::lock_guard lock{s_metricsMutex};
    s_metrics = metrics;
}
@end

namespace dusk::duo {
Metrics native_metrics() {
    std::lock_guard lock{s_metricsMutex};
    return s_metrics;
}
bool attach_observer(SDL_Window* window) {
    struct Request { SDL_Window* window; bool attached = false; } request{window};
    const bool called = SDL_RunOnMainThread([](void* userdata) {
        auto& request = *static_cast<Request*>(userdata);
        UIWindow* nativeWindow = (__bridge UIWindow*)SDL_GetPointerProperty(
            SDL_GetWindowProperties(request.window), SDL_PROP_WINDOW_UIKIT_WINDOW_POINTER, nullptr);
        UIView* view = nativeWindow.rootViewController.view;
        if (view) {
            DuskDuoObserver* observer = [[DuskDuoObserver alloc] initWithFrame:view.bounds];
            [view addSubview:observer];
            [observer publishMetrics];
            request.attached = true;
        }
    }, &request, true);
    return called && request.attached;
}
} // namespace dusk::duo
