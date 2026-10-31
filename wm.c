#define _POSIX_C_SOURCE 200809L

#define AX_IMPLEMENTATION
#include "./thirdparty/axiom.h"

#include "wm.h"
#include "config.h"

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

#define ADACHI_NUMLOCK_MASK Mod2Mask
#define ADACHI_MODS_MASK (ShiftMask | ControlMask | Mod1Mask | Mod4Mask)

typedef enum {
    FRAME_ROWS,
    FRAME_COLUMNS,
} Frame_Orientation;

struct Adachi_Frame {
    Adachi_Frame *parent;
    Adachi_Frame *first;
    Adachi_Frame *second;
    Frame_Orientation orientation;
    float ratio;
    Window window;
    int x;
    int y;
    int width;
    int height;
};

typedef void Adachi_Action(Adachi_WM *wm, const char *arg);

typedef struct {
    KeySym key;
    Adachi_Action *action;
    const char *arg;
} Adachi_Binding;

static void wm_spawn(Adachi_WM *wm, const char *command);
static void wm_next(Adachi_WM *wm, const char *arg);
static void wm_prev(Adachi_WM *wm, const char *arg);
static void wm_other(Adachi_WM *wm, const char *arg);
static void wm_kill(Adachi_WM *wm, const char *arg);
static void wm_quit(Adachi_WM *wm, const char *arg);
static void wm_split_rows(Adachi_WM *wm, const char *arg);
static void wm_split_columns(Adachi_WM *wm, const char *arg);
static void wm_frame_next(Adachi_WM *wm, const char *arg);
static void wm_frame_left(Adachi_WM *wm, const char *arg);
static void wm_frame_right(Adachi_WM *wm, const char *arg);
static void wm_frame_up(Adachi_WM *wm, const char *arg);
static void wm_frame_down(Adachi_WM *wm, const char *arg);
static void wm_resize(Adachi_WM *wm, const char *arg);
static void wm_only(Adachi_WM *wm, const char *arg);
static void wm_remove(Adachi_WM *wm, const char *arg);

#define ADACHI_BIND(key, action, arg) { key, action, arg },
static const Adachi_Binding bindings[] = { ADACHI_BINDINGS };
#undef ADACHI_BIND

#define ADACHI_RUN(command) command,
static const char *startup_commands[] = { ADACHI_STARTUP_COMMANDS NULL };
#undef ADACHI_RUN

static bool wm_other_running = false;

static void adachi_log_error(const char *fmt, ...)
{
    fprintf(stderr, "adachi: ERROR: ");

    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);

    fprintf(stderr, "\n");
}

static Adachi_Frame *frame_new(void)
{
    Adachi_Frame *f = calloc(1, sizeof(*f));
    AX_ASSERT(f != NULL);
    return f;
}

static void frame_free(Adachi_Frame *f)
{
    if (f == NULL) return;
    frame_free(f->first);
    frame_free(f->second);
    free(f);
}

static void workspace_init(Adachi_Workspace *workspace)
{
    workspace->windows = ax_array(Window);
    workspace->root_frame = frame_new();
    workspace->frame = workspace->root_frame;
}

static void workspace_free(Adachi_Workspace *workspace)
{
    frame_free(workspace->root_frame);
    ax_array_release(&workspace->windows);
    *workspace = (Adachi_Workspace) {0};
}

static Adachi_Frame *frame_leftmost(Adachi_Frame *f)
{
    while (f->first != NULL) f = f->first;
    return f;
}

static Adachi_Frame *frame_next(Adachi_Frame *f)
{
    for (; f->parent != NULL; f = f->parent) {
        if (f == f->parent->first) return frame_leftmost(f->parent->second);
    }
    return frame_leftmost(f);
}

static Adachi_Frame *frame_find(Adachi_Frame *f, Window w)
{
    if (f->first == NULL) return f->window == w ? f : NULL;
    Adachi_Frame *found = frame_find(f->first, w);
    return found != NULL ? found : frame_find(f->second, w);
}

static Adachi_Frame *frame_at(Adachi_Frame *f, int x, int y)
{
    if (x < f->x || x >= f->x + f->width || y < f->y || y >= f->y + f->height) return NULL;
    if (f->first == NULL) return f;
    Adachi_Frame *hit = frame_at(f->first, x, y);
    return hit != NULL ? hit : frame_at(f->second, x, y);
}

static void frame_adjust_ratio(Adachi_Frame *f, Frame_Orientation orientation, int delta)
{
    if (delta == 0) return;

    while (f->parent != NULL && f->parent->orientation != orientation) f = f->parent;
    Adachi_Frame *parent = f->parent;
    if (parent == NULL) return;

    int extent = orientation == FRAME_COLUMNS ? parent->width : parent->height;
    if (extent < 2*ADACHI_FRAME_MIN) return;

    float shift = (float)delta/(float)extent;
    float min = (float)ADACHI_FRAME_MIN/(float)extent;

    float ratio = parent->ratio + (f == parent->first ? shift : -shift);
    parent->ratio = ax_clampf(ratio, min, 1.0f - min);
}

static int window_size(int value)
{
    return value > 1 ? value : 1;
}

static void frame_layout(Adachi_WM *wm, Adachi_Frame *f, int x, int y, int width, int height)
{
    f->x = x;
    f->y = y;
    f->width = width;
    f->height = height;

    if (f->first == NULL) {
        if (f->window == None) return;
        int border = ADACHI_BORDER_WIDTH;
        XSetWindowBorder(wm->dpy, f->window, f == wm->workspace->frame ? ADACHI_BORDER_FOCUS : ADACHI_BORDER_NORMAL);
        XMoveResizeWindow(wm->dpy, f->window, x, y,
                          (unsigned int)window_size(width - 2*border),
                          (unsigned int)window_size(height - 2*border));
        return;
    }

    if (f->orientation == FRAME_COLUMNS) {
        int first = (int)((float)width*f->ratio);
        frame_layout(wm, f->first, x, y, first, height);
        frame_layout(wm, f->second, x + first, y, width - first, height);
    } else {
        int first = (int)((float)height*f->ratio);
        frame_layout(wm, f->first, x, y, width, first);
        frame_layout(wm, f->second, x, y + first, width, height - first);
    }
}

static int wm_error_startup(Display *dpy, XErrorEvent *e)
{
    AX_UNUSED(dpy);
    AX_UNUSED(e);
    wm_other_running = true;
    return 0;
}

static int wm_error(Display *dpy, XErrorEvent *e)
{
    if (e->error_code == BadWindow) return 0;
    if (e->error_code == BadMatch && (e->request_code == X_SetInputFocus || e->request_code == X_ConfigureWindow)) return 0;

    char text[128] = {0};
    XGetErrorText(dpy, e->error_code, text, sizeof(text));
    adachi_log_error("X request %d: %s", e->request_code, text);
    return 0;
}

static bool workspace_find_window(const Adachi_Workspace *workspace, Window window, size_t *index)
{
    Window *windows = workspace->windows.data;
    for (size_t i = 0; i < workspace->windows.length; ++i) {
        if (windows[i] == window) {
            if (index != NULL) *index = i;
            return true;
        }
    }
    return false;
}

static Adachi_Workspace *wm_find_workspace(Adachi_WM *wm, Window window, size_t *index)
{
    for (size_t i = 0; i < ADACHI_WORKSPACES; ++i) {
        if (workspace_find_window(&wm->workspaces[i], window, index)) return &wm->workspaces[i];
    }
    return NULL;
}

static Window workspace_hidden_from(const Adachi_Workspace *workspace, size_t index, bool forward)
{
    size_t count = workspace->windows.length;
    if (count == 0) return None;

    index %= count;
    for (size_t i = 0; i < count; ++i) {
        Window window = *ax_array_at_as(&workspace->windows, index, Window);
        if (frame_find(workspace->root_frame, window) == NULL) return window;
        index = forward ? (index + 1) % count : (index + count - 1) % count;
    }
    return None;
}

static Window workspace_hidden_after(const Adachi_Workspace *workspace, Window window, bool forward)
{
    size_t count = workspace->windows.length;
    if (count == 0) return None;

    size_t index = 0;
    if (workspace_find_window(workspace, window, &index)) {
        index = forward ? (index + 1) % count : (index + count - 1) % count;
    } else {
        index = forward ? 0 : count - 1;
    }
    return workspace_hidden_from(workspace, index, forward);
}

static void wm_refresh(Adachi_WM *wm)
{
    Window *windows = wm->workspace->windows.data;
    for (size_t i = 0; i < wm->workspace->windows.length; ++i) {
        Window w = windows[i];
        if (frame_find(wm->workspace->root_frame, w) == NULL) XMoveWindow(wm->dpy, w, -2*wm->width, 0);
    }

    frame_layout(wm, wm->workspace->root_frame, 0, 0, wm->width, wm->height);

    Window w = wm->workspace->frame->window;
    XSetInputFocus(wm->dpy, w != None ? w : wm->root, RevertToPointerRoot, CurrentTime);
}

static void wm_focus_frame(Adachi_WM *wm, Adachi_Frame *frame)
{
    Window old = wm->workspace->frame->window;
    wm->workspace->frame = frame;
    if (old != None && old != frame->window) wm->workspace->last = old;
    wm_refresh(wm);
}

static void wm_show_window(Adachi_WM *wm, Window window)
{
    Adachi_Frame *frame = frame_find(wm->workspace->root_frame, window);
    if (frame == NULL) {
        Window old = wm->workspace->frame->window;
        wm->workspace->frame->window = window;
        if (old != None && old != window) wm->workspace->last = old;
        wm_refresh(wm);
        return;
    }
    wm_focus_frame(wm, frame);
}

static void workspace_replace_frame(Adachi_Workspace *workspace, Adachi_Frame *from, Adachi_Frame *to)
{
    to->parent = from->parent;
    if (from->parent == NULL) workspace->root_frame = to;
    else if (from->parent->first == from) from->parent->first = to;
    else from->parent->second = to;
}

static void wm_switch_workspace(Adachi_WM *wm, Adachi_Workspace *workspace)
{
    if (workspace == wm->workspace) return;

    Window *windows = wm->workspace->windows.data;
    for (size_t i = 0; i < wm->workspace->windows.length; ++i) {
        XMoveWindow(wm->dpy, windows[i], -2*wm->width, 0);
    }

    wm->workspace = workspace;
    wm_refresh(wm);
}

static void wm_manage(Adachi_WM *wm, Window window)
{
    Adachi_Workspace *workspace = wm_find_workspace(wm, window, NULL);
    if (workspace == NULL) {
        XSetWindowBorderWidth(wm->dpy, window, ADACHI_BORDER_WIDTH);
        ax_array_push_value(&wm->workspace->windows, Window, window);
    } else {
        wm_switch_workspace(wm, workspace);
    }
    XMapWindow(wm->dpy, window);
    wm_show_window(wm, window);
}

static bool workspace_remove_window(Adachi_Workspace *workspace, Window window)
{
    size_t index = 0;
    if (!workspace_find_window(workspace, window, &index)) return false;

    ax_array_remove(&workspace->windows, index);
    if (workspace->last == window) workspace->last = None;

    Adachi_Frame *f = frame_find(workspace->root_frame, window);
    if (f == NULL) return true;

    Window next = workspace->last;
    if (next == None || frame_find(workspace->root_frame, next) != NULL) {
        next = workspace_hidden_from(workspace, index, true);
    }

    if (next == workspace->last) workspace->last = None;

    f->window = next;
    return true;
}

static void wm_unmanage(Adachi_WM *wm, Window window)
{
    Adachi_Workspace *workspace = wm_find_workspace(wm, window, NULL);
    if (workspace != NULL && workspace_remove_window(workspace, window) && workspace == wm->workspace) {
        wm_refresh(wm);
    }
}

static void wm_move_to_workspace(Adachi_WM *wm, Adachi_Workspace *destination)
{
    Adachi_Workspace *source = wm->workspace;
    Window window = source->frame->window;
    if (window == None || destination == source) return;

    workspace_remove_window(source, window);
    ax_array_push_value(&destination->windows, Window, window);
    if (destination->frame->window == None) destination->frame->window = window;

    XMoveWindow(wm->dpy, window, -2*wm->width, 0);
    wm_refresh(wm);
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

static void wm_cycle(Adachi_WM *wm, bool forward)
{
    Window window = workspace_hidden_after(wm->workspace, wm->workspace->frame->window, forward);
    if (window != None) wm_show_window(wm, window);
}

static void wm_split(Adachi_WM *wm, Frame_Orientation orientation)
{
    Adachi_Frame *f = wm->workspace->frame;
    int extent = orientation == FRAME_COLUMNS ? f->width : f->height;
    if (extent < 2*ADACHI_FRAME_MIN) return;

    Adachi_Frame *parent = frame_new();
    Adachi_Frame *sibling = frame_new();

    workspace_replace_frame(wm->workspace, f, parent);
    parent->orientation = orientation;
    parent->ratio = 0.5f;
    parent->first = f;
    parent->second = sibling;
    f->parent = parent;
    sibling->parent = parent;

    sibling->window = workspace_hidden_after(wm->workspace, f->window, true);
    wm_refresh(wm);
}

static void wm_frame_direction(Adachi_WM *wm, int dx, int dy)
{
    Adachi_Frame *f = wm->workspace->frame;
    int x = f->x + f->width/2 + dx*(f->width/2 + 1);
    int y = f->y + f->height/2 + dy*(f->height/2 + 1);

    Adachi_Frame *target = frame_at(wm->workspace->root_frame, x, y);
    if (target != NULL) wm_focus_frame(wm, target);
}

static void wm_adjust_frame(Adachi_WM *wm, int dx, int dy)
{
    frame_adjust_ratio(wm->workspace->frame, FRAME_COLUMNS, dx);
    frame_adjust_ratio(wm->workspace->frame, FRAME_ROWS, dy);
    wm_refresh(wm);
}

static bool wm_grab_keyboard(Adachi_WM *wm)
{
    int status = XGrabKeyboard(wm->dpy, wm->root, False, GrabModeAsync, GrabModeAsync, CurrentTime);
    if (status == GrabSuccess) return true;
    adachi_log_error("Could not grab the keyboard (status %d)", status);
    return false;
}

static void wm_spawn(Adachi_WM *wm, const char *command)
{
    if (command == NULL) return;

    pid_t pid = fork();
    if (pid < 0) {
        adachi_log_error("fork: %s", strerror(errno));
        return;
    }
    if (pid > 0) return;

    close(ConnectionNumber(wm->dpy));
    setsid();
    signal(SIGCHLD, SIG_DFL);
    execl("/bin/sh", "sh", "-c", command, (char *)NULL);
    _exit(127);
}

static void wm_next(Adachi_WM *wm, const char *arg)
{
    AX_UNUSED(arg);
    wm_cycle(wm, true);
}

static void wm_prev(Adachi_WM *wm, const char *arg)
{
    AX_UNUSED(arg);
    wm_cycle(wm, false);
}

static void wm_other(Adachi_WM *wm, const char *arg)
{
    AX_UNUSED(arg);
    if (wm->workspace->last != None && workspace_find_window(wm->workspace, wm->workspace->last, NULL)) {
        wm_show_window(wm, wm->workspace->last);
    }
}

static void wm_kill(Adachi_WM *wm, const char *arg)
{
    AX_UNUSED(arg);
    Window w = wm->workspace->frame->window;
    if (w == None) return;

    if (!wm_supports_delete(wm, w)) {
        XKillClient(wm->dpy, w);
        return;
    }

    XEvent e = {0};
    e.xclient.type = ClientMessage;
    e.xclient.window = w;
    e.xclient.message_type = wm->wm_protocols;
    e.xclient.format = 32;
    e.xclient.data.l[0] = (long)wm->wm_delete;
    e.xclient.data.l[1] = CurrentTime;
    XSendEvent(wm->dpy, w, False, NoEventMask, &e);
}

static void wm_quit(Adachi_WM *wm, const char *arg)
{
    AX_UNUSED(arg);
    wm->running = false;
}

static void wm_split_rows(Adachi_WM *wm, const char *arg)
{
    AX_UNUSED(arg);
    wm_split(wm, FRAME_ROWS);
}

static void wm_split_columns(Adachi_WM *wm, const char *arg)
{
    AX_UNUSED(arg);
    wm_split(wm, FRAME_COLUMNS);
}

static void wm_frame_next(Adachi_WM *wm, const char *arg)
{
    AX_UNUSED(arg);
    wm_focus_frame(wm, frame_next(wm->workspace->frame));
}

static void wm_frame_left(Adachi_WM *wm, const char *arg)
{
    AX_UNUSED(arg);
    wm_frame_direction(wm, -1, 0);
}

static void wm_frame_right(Adachi_WM *wm, const char *arg)
{
    AX_UNUSED(arg);
    wm_frame_direction(wm, 1, 0);
}

static void wm_frame_up(Adachi_WM *wm, const char *arg)
{
    AX_UNUSED(arg);
    wm_frame_direction(wm, 0, -1);
}

static void wm_frame_down(Adachi_WM *wm, const char *arg)
{
    AX_UNUSED(arg);
    wm_frame_direction(wm, 0, 1);
}

static void wm_resize(Adachi_WM *wm, const char *arg)
{
    AX_UNUSED(arg);
    if (wm->workspace->root_frame->first == NULL) return;
    if (!wm_grab_keyboard(wm)) return;
    wm->resizing = true;
}

static void wm_only(Adachi_WM *wm, const char *arg)
{
    AX_UNUSED(arg);
    Adachi_Frame *old_root = wm->workspace->root_frame;
    Adachi_Frame *only = frame_new();
    only->window = wm->workspace->frame->window;

    wm->workspace->root_frame = only;
    wm->workspace->frame = only;
    frame_free(old_root);
    wm_refresh(wm);
}

static void wm_remove(Adachi_WM *wm, const char *arg)
{
    AX_UNUSED(arg);
    Adachi_Frame *f = wm->workspace->frame;
    Adachi_Frame *parent = f->parent;
    if (parent == NULL) return;

    Adachi_Frame *sibling = parent->first == f ? parent->second : parent->first;
    Adachi_Frame *target = frame_leftmost(sibling);
    Window old = f->window;

    workspace_replace_frame(wm->workspace, parent, sibling);
    wm->workspace->frame = target;
    if (old != None && old != target->window) wm->workspace->last = old;
    free(f);
    free(parent);
    wm_refresh(wm);
}

static void wm_grab_prefix(Adachi_WM *wm)
{
    KeyCode code = XKeysymToKeycode(wm->dpy, ADACHI_PREFIX_KEY);
    unsigned int locks[] = { 0, LockMask, ADACHI_NUMLOCK_MASK, LockMask | ADACHI_NUMLOCK_MASK };
    for (size_t i = 0; i < AX_ARRAY_LEN(locks); ++i) {
        XGrabKey(wm->dpy, code, ADACHI_PREFIX_MOD | locks[i], wm->root, False, GrabModeAsync, GrabModeAsync);
    }
}

static bool wm_on_workspace_key(Adachi_WM *wm, XKeyEvent *e)
{
    unsigned int modifiers = e->state & ADACHI_MODS_MASK;
    if (modifiers != 0 && modifiers != ShiftMask) return false;

    KeySym key = XLookupKeysym(e, 0);
    if (key < XK_1 || key >= XK_1 + ADACHI_WORKSPACES) return false;

    Adachi_Workspace *workspace = &wm->workspaces[key - XK_1];
    if (modifiers == ShiftMask) wm_move_to_workspace(wm, workspace);
    else wm_switch_workspace(wm, workspace);
    return true;
}

static void wm_on_key_press(Adachi_WM *wm, XKeyEvent *e)
{
    KeySym key = XLookupKeysym(e, (e->state & ShiftMask) ? 1 : 0);

    if (wm->resizing) {
        if (IsModifierKey(key)) return;
        switch (key) {
        case XK_Left:  wm_adjust_frame(wm, -ADACHI_RESIZE_STEP, 0); break;
        case XK_Right: wm_adjust_frame(wm,  ADACHI_RESIZE_STEP, 0); break;
        case XK_Up:    wm_adjust_frame(wm, 0, -ADACHI_RESIZE_STEP); break;
        case XK_Down:  wm_adjust_frame(wm, 0,  ADACHI_RESIZE_STEP); break;
        default:
            XUngrabKeyboard(wm->dpy, CurrentTime);
            wm->resizing = false;
        }
        return;
    }

    if (!wm->waiting) {
        if (key != ADACHI_PREFIX_KEY || (e->state & ADACHI_MODS_MASK) != ADACHI_PREFIX_MOD) return;
        if (!wm_grab_keyboard(wm)) return;
        wm->waiting = true;
        return;
    }

    if (IsModifierKey(key)) return;

    XUngrabKeyboard(wm->dpy, CurrentTime);
    wm->waiting = false;

    if (wm_on_workspace_key(wm, e)) return;

    for (size_t i = 0; i < AX_ARRAY_LEN(bindings); ++i) {
        if (bindings[i].key == key) {
            bindings[i].action(wm, bindings[i].arg);
            return;
        }
    }
}

static void wm_on_configure_request(Adachi_WM *wm, XConfigureRequestEvent *e)
{
    if (wm_find_workspace(wm, e->window, NULL) != NULL) {
        XWindowAttributes attr = {0};
        if (!XGetWindowAttributes(wm->dpy, e->window, &attr)) return;

        XConfigureEvent ce = {
            .type = ConfigureNotify,
            .display = wm->dpy,
            .event = e->window,
            .window = e->window,
            .x = attr.x,
            .y = attr.y,
            .width = attr.width,
            .height = attr.height,
            .border_width = attr.border_width,
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
        adachi_log_error("Could not open the X display");
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
        adachi_log_error("Another window manager is already running");
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

    for (size_t i = 0; i < ADACHI_WORKSPACES; ++i) {
        workspace_init(&wm->workspaces[i]);
    }
    wm->workspace = &wm->workspaces[0];
    wm_refresh(wm);
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
        case ConfigureRequest: wm_on_configure_request(wm, &e.xconfigurerequest);   break;
        case KeyPress:         wm_on_key_press(wm, &e.xkey);                        break;
        default: break;
        }
    }
}

void adachi_wm_free(Adachi_WM *wm)
{
    for (size_t i = 0; i < ADACHI_WORKSPACES; ++i) {
        workspace_free(&wm->workspaces[i]);
    }
    if (wm->dpy != NULL) XCloseDisplay(wm->dpy);
    *wm = (Adachi_WM) {0};
}
