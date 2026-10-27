#define _POSIX_C_SOURCE 200809L

#include "wm.h"
#include "config.h"

#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <X11/Xproto.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>

#define ADACHI_ARRAY_LEN(array) (sizeof(array)/sizeof((array)[0]))
#define ADACHI_WINDOW_INIT_CAPACITY 16
#define ADACHI_NUMLOCK_MASK Mod2Mask
#define ADACHI_MODS_MASK (ShiftMask | ControlMask | Mod1Mask | Mod4Mask)

typedef void Adachi_Action(Adachi_WM *wm, const char *arg);

typedef struct {
    KeySym key;
    Adachi_Action *action;
    const char *arg;
} Adachi_Binding;

static void wm_exec(Adachi_WM *wm, const char *arg);
static void wm_next(Adachi_WM *wm, const char *arg);
static void wm_prev(Adachi_WM *wm, const char *arg);
static void wm_other(Adachi_WM *wm, const char *arg);
static void wm_select(Adachi_WM *wm, const char *arg);
static void wm_kill(Adachi_WM *wm, const char *arg);
static void wm_quit(Adachi_WM *wm, const char *arg);

#define ADACHI_BIND(key, action, arg) { key, action, arg },
static const Adachi_Binding bindings[] = { ADACHI_BINDINGS };
#undef ADACHI_BIND

#define ADACHI_RUN(command) command,
static const char *startup_commands[] = { ADACHI_STARTUP_COMMANDS NULL };
#undef ADACHI_RUN

static bool wm_other_running = false;

static void log_error(const char *fmt, ...)
{
    fprintf(stderr, "adachi: ERROR: ");

    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);

    fprintf(stderr, "\n");
}

static void windows_append(Adachi_WM *wm, Window window)
{
    if (wm->windows.count == wm->windows.capacity) {
        size_t capacity = wm->windows.capacity == 0 ? ADACHI_WINDOW_INIT_CAPACITY : wm->windows.capacity*2;

        Window *items = realloc(wm->windows.items, capacity*sizeof(*items));
        assert(items != NULL);
        wm->windows.items = items;
        wm->windows.capacity = capacity;
    }
    wm->windows.items[wm->windows.count++] = window;
}

static void windows_remove(Adachi_WM *wm, size_t index)
{
    assert(index < wm->windows.count);
    memmove(&wm->windows.items[index], &wm->windows.items[index + 1], (wm->windows.count - index - 1)*sizeof(*wm->windows.items));
    wm->windows.count -= 1;
}

static int wm_error_startup(Display *dpy, XErrorEvent *e)
{
    (void)dpy;
    (void)e;
    wm_other_running = true;
    return 0;
}

static int wm_error(Display *dpy, XErrorEvent *e)
{
    if (e->error_code == BadWindow) return 0;
    if (e->error_code == BadMatch && (e->request_code == X_SetInputFocus || e->request_code == X_ConfigureWindow)) return 0;

    char text[128] = {0};
    XGetErrorText(dpy, e->error_code, text, sizeof(text));
    log_error("X request %d: %s", e->request_code, text);
    return 0;
}

static bool wm_find(const Adachi_WM *wm, Window w, size_t *index)
{
    for (size_t i = 0; i < wm->windows.count; ++i) {
        if (wm->windows.items[i] == w) {
            if (index != NULL) *index = i;
            return true;
        }
    }
    return false;
}

static void wm_focus(Adachi_WM *wm, Window w)
{
    if (w == None) {
        wm->current = None;
        XSetInputFocus(wm->dpy, wm->root, RevertToPointerRoot, CurrentTime);
        return;
    }

    if (w != wm->current) {
        wm->last = wm->current;
        wm->current = w;
    }

    XMoveResizeWindow(wm->dpy, w, 0, 0, (unsigned int)wm->width, (unsigned int)wm->height);
    XRaiseWindow(wm->dpy, w);
    XSetInputFocus(wm->dpy, w, RevertToPointerRoot, CurrentTime);
}

static void wm_manage(Adachi_WM *wm, Window w)
{
    if (!wm_find(wm, w, NULL)) {
        XSetWindowBorderWidth(wm->dpy, w, 0);
        windows_append(wm, w);
    }
    XMapWindow(wm->dpy, w);
    wm_focus(wm, w);
}

static void wm_unmanage(Adachi_WM *wm, Window w)
{
    size_t index = 0;
    if (!wm_find(wm, w, &index)) return;

    windows_remove(wm, index);
    if (wm->last == w) wm->last = None;
    if (wm->current != w) return;

    Window next = wm->last;
    if (next == None && wm->windows.count > 0) {
        size_t i = index < wm->windows.count ? index : wm->windows.count - 1;
        next = wm->windows.items[i];
    }
    wm->current = None;
    wm_focus(wm, next);
}

static void wm_scan(Adachi_WM *wm)
{
    Window root = None;
    Window parent = None;
    Window *children = NULL;
    unsigned int count = 0;

    if (!XQueryTree(wm->dpy, wm->root, &root, &parent, &children, &count)) return;
    for (unsigned int i = 0; i < count; ++i) {
        XWindowAttributes attr = {0};
        if (!XGetWindowAttributes(wm->dpy, children[i], &attr)) continue;
        if (attr.override_redirect || attr.map_state != IsViewable) continue;
        wm_manage(wm, children[i]);
    }
    if (children != NULL) XFree(children);
}

static bool wm_supports_delete(const Adachi_WM *wm, Window w)
{
    Atom *protocols = NULL;
    int count = 0;

    if (!XGetWMProtocols(wm->dpy, w, &protocols, &count)) return false;
    bool result = false;
    for (int i = 0; i < count; ++i) {
        if (protocols[i] == wm->wm_delete) {
            result = true;
            break;
        }
    }

    if (protocols != NULL) XFree(protocols);
    return result;
}

static void wm_cycle(Adachi_WM *wm, size_t offset)
{
    if (wm->windows.count == 0) return;
    size_t index = 0;
    wm_find(wm, wm->current, &index);
    wm_focus(wm, wm->windows.items[(index + offset) % wm->windows.count]);
}

static void wm_spawn(Adachi_WM *wm, const char *command)
{
    if (command == NULL) return;

    pid_t pid = fork();
    if (pid < 0) {
        log_error("fork: %s", strerror(errno));
        return;
    }
    if (pid > 0) return;

    close(ConnectionNumber(wm->dpy));
    setsid();
    signal(SIGCHLD, SIG_DFL);
    execl("/bin/sh", "sh", "-c", command, (char *)NULL);
    _exit(127);
}

static void wm_exec(Adachi_WM *wm, const char *arg)
{
    wm_spawn(wm, arg);
}

static void wm_next(Adachi_WM *wm, const char *arg)
{
    (void)arg;
    wm_cycle(wm, 1);
}

static void wm_prev(Adachi_WM *wm, const char *arg)
{
    (void)arg;
    wm_cycle(wm, wm->windows.count - 1);
}

static void wm_other(Adachi_WM *wm, const char *arg)
{
    (void)arg;
    if (wm->last != None && wm_find(wm, wm->last, NULL)) wm_focus(wm, wm->last);
}

static void wm_select(Adachi_WM *wm, const char *arg)
{
    if (arg == NULL) return;
    size_t index = strtoul(arg, NULL, 10);
    if (index < wm->windows.count) wm_focus(wm, wm->windows.items[index]);
}

static void wm_kill(Adachi_WM *wm, const char *arg)
{
    (void)arg;
    if (wm->current == None) return;

    if (!wm_supports_delete(wm, wm->current)) {
        XKillClient(wm->dpy, wm->current);
        return;
    }

    XEvent e = {0};
    e.xclient.type = ClientMessage;
    e.xclient.window = wm->current;
    e.xclient.message_type = wm->wm_protocols;
    e.xclient.format = 32;
    e.xclient.data.l[0] = (long)wm->wm_delete;
    e.xclient.data.l[1] = CurrentTime;
    XSendEvent(wm->dpy, wm->current, False, NoEventMask, &e);
}

static void wm_quit(Adachi_WM *wm, const char *arg)
{
    (void)arg;
    wm->running = false;
}

static void wm_grab_prefix(Adachi_WM *wm)
{
    KeyCode code = XKeysymToKeycode(wm->dpy, ADACHI_PREFIX_KEY);
    unsigned int locks[] = { 0, LockMask, ADACHI_NUMLOCK_MASK, LockMask | ADACHI_NUMLOCK_MASK };
    for (size_t i = 0; i < ADACHI_ARRAY_LEN(locks); ++i) {
        XGrabKey(wm->dpy, code, ADACHI_PREFIX_MOD | locks[i], wm->root, False, GrabModeAsync, GrabModeAsync);
    }
}

static void wm_on_key_press(Adachi_WM *wm, XKeyEvent *e)
{
    KeySym key = XLookupKeysym(e, (e->state & ShiftMask) ? 1 : 0);

    if (!wm->waiting) {
        if (key != ADACHI_PREFIX_KEY || (e->state & ADACHI_MODS_MASK) != ADACHI_PREFIX_MOD) return;
        int status = XGrabKeyboard(wm->dpy, wm->root, False, GrabModeAsync, GrabModeAsync, CurrentTime);
        if (status != GrabSuccess) {
            log_error("Could not grab the keyboard (status %d)", status);
            return;
        }
        wm->waiting = true;
        return;
    }

    if (IsModifierKey(key)) return;

    XUngrabKeyboard(wm->dpy, CurrentTime);
    wm->waiting = false;

    for (size_t i = 0; i < ADACHI_ARRAY_LEN(bindings); ++i) {
        if (bindings[i].key == key) {
            bindings[i].action(wm, bindings[i].arg);
            return;
        }
    }
}

static void wm_on_configure_request(Adachi_WM *wm, XConfigureRequestEvent *e)
{
    if (wm_find(wm, e->window, NULL)) {
        XConfigureEvent ce = {
            .type = ConfigureNotify,
            .display = wm->dpy,
            .event = e->window,
            .window = e->window,
            .x = 0,
            .y = 0,
            .width = wm->width,
            .height = wm->height,
            .border_width = 0,
            .above = None,
            .override_redirect = False,
        };
        XSendEvent(wm->dpy, e->window, False, StructureNotifyMask, (XEvent *)&ce);
        return;
    }

    XWindowChanges wc = {
        .x = e->x,
        .y = e->y,
        .width = e->width,
        .height = e->height,
        .border_width = e->border_width,
        .sibling = e->above,
        .stack_mode = e->detail,
    };

    XConfigureWindow(wm->dpy, e->window, (unsigned int)e->value_mask, &wc);
}

bool adachi_wm_init(Adachi_WM *wm)
{
    wm->dpy = XOpenDisplay(NULL);
    if (wm->dpy == NULL) {
        log_error("Could not open the X display");
        return false;
    }

    int screen = DefaultScreen(wm->dpy);

    wm->root   = RootWindow(wm->dpy, screen);
    wm->width  = DisplayWidth(wm->dpy, screen);
    wm->height = DisplayHeight(wm->dpy, screen);

    wm->wm_protocols = XInternAtom(wm->dpy, "WM_PROTOCOLS", False);
    wm->wm_delete    = XInternAtom(wm->dpy, "WM_DELETE_WINDOW", False);

    XSetErrorHandler(wm_error_startup);
    XSelectInput(wm->dpy, wm->root, SubstructureRedirectMask | SubstructureNotifyMask);
    XSync(wm->dpy, False);

    if (wm_other_running) {
        log_error("Another window manager is already running");
        XCloseDisplay(wm->dpy);
        wm->dpy = NULL;
        return false;
    }

    XSetErrorHandler(wm_error);

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    
    sa.sa_handler = SIG_IGN;
    sa.sa_flags = SA_NOCLDSTOP | SA_NOCLDWAIT | SA_RESTART;

    sigemptyset(&sa.sa_mask);
    sigaction(SIGCHLD, &sa, NULL);

    for (size_t i = 0; startup_commands[i] != NULL; ++i) {
        wm_spawn(wm, startup_commands[i]);
    }

    XDefineCursor(wm->dpy, wm->root, XCreateFontCursor(wm->dpy, XC_left_ptr));
    wm_grab_prefix(wm);
    wm_focus(wm, None);
    wm_scan(wm);
    XSync(wm->dpy, False);
    return true;
}

void adachi_wm_run(Adachi_WM *wm)
{
    wm->running = true;
    while (wm->running) {
        XEvent e = {0};
        XNextEvent(wm->dpy, &e);
        switch (e.type) {
        case MapRequest:       wm_manage(wm, e.xmaprequest.window);                break;
        case UnmapNotify:      wm_unmanage(wm, e.xunmap.window);                   break;
        case DestroyNotify:    wm_unmanage(wm, e.xdestroywindow.window);           break;
        case ConfigureRequest: wm_on_configure_request(wm, &e.xconfigurerequest);  break;
        case KeyPress:         wm_on_key_press(wm, &e.xkey);                       break;
        default: break;
        }
    }
}

void adachi_wm_free(Adachi_WM *wm)
{
    free(wm->windows.items);
    if (wm->dpy != NULL) XCloseDisplay(wm->dpy);
    *wm = (Adachi_WM) {0};
}
