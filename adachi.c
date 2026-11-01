#define _POSIX_C_SOURCE 200809L

#include "adachi.h"

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

#define UNUSED(x) ((void)(x))
#define ARRAY_LEN(xs) (sizeof(xs)/sizeof((xs)[0]))

static Window adachi_workspace_hidden_from(const Adachi_Workspace *workspace, size_t index, bool forward);
static void adachi_keys_grab_prefix(Adachi_WM *wm);
static void adachi_keys_on_press(Adachi_WM *wm, XKeyEvent *e);

static void adachi_frame_replace(Adachi_Frame **root, Adachi_Frame *from, Adachi_Frame *to)
{
    to->parent = from->parent;
    if (from->parent == NULL) *root = to;
    else if (from->parent->first == from) from->parent->first = to;
    else from->parent->second = to;
}

static Adachi_Frame *adachi_frame_new(void)
{
    Adachi_Frame *f = calloc(1, sizeof(*f));
    assert(f != NULL);
    return f;
}

static void adachi_frame_free(Adachi_Frame *f)
{
    if (f == NULL) return;
    adachi_frame_free(f->first);
    adachi_frame_free(f->second);
    free(f);
}

static Adachi_Frame *adachi_frame_leftmost(Adachi_Frame *f)
{
    while (f->first != NULL) f = f->first;
    return f;
}

static Adachi_Frame *adachi_frame_next(Adachi_Frame *f)
{
    for (; f->parent != NULL; f = f->parent) {
        if (f == f->parent->first) return adachi_frame_leftmost(f->parent->second);
    }
    return adachi_frame_leftmost(f);
}

static Adachi_Frame *adachi_frame_find(Adachi_Frame *f, Window w)
{
    if (f->first == NULL) return f->window == w ? f : NULL;
    Adachi_Frame *found = adachi_frame_find(f->first, w);
    return found != NULL ? found : adachi_frame_find(f->second, w);
}

static Adachi_Frame *adachi_frame_at(Adachi_Frame *f, int x, int y)
{
    if (x < f->x || x >= f->x + f->width || y < f->y || y >= f->y + f->height) return NULL;
    if (f->first == NULL) return f;
    Adachi_Frame *hit = adachi_frame_at(f->first, x, y);
    return hit != NULL ? hit : adachi_frame_at(f->second, x, y);
}

static Adachi_Frame *adachi_frame_split(Adachi_Frame **root, Adachi_Frame *f, Adachi_Frame_Orientation orientation)
{
    Adachi_Frame *parent = adachi_frame_new();
    Adachi_Frame *sibling = adachi_frame_new();
    adachi_frame_replace(root, f, parent);
    parent->orientation = orientation;
    parent->ratio = 0.5f;
    parent->first = f;
    parent->second = sibling;
    f->parent = parent;
    sibling->parent = parent;
    return sibling;
}

static Adachi_Frame *adachi_frame_remove(Adachi_Frame **root, Adachi_Frame *f)
{
    Adachi_Frame *parent = f->parent;
    if (parent == NULL) return NULL;
    Adachi_Frame *sibling = parent->first == f ? parent->second : parent->first;
    adachi_frame_replace(root, parent, sibling);
    free(f);
    free(parent);
    return adachi_frame_leftmost(sibling);
}

static void adachi_frame_adjust_ratio(Adachi_Frame *f, Adachi_Frame_Orientation orientation, int delta, int min_size)
{
    if (delta == 0) return;
    while (f->parent != NULL && f->parent->orientation != orientation) f = f->parent;
    Adachi_Frame *parent = f->parent;

    if (parent == NULL) return;
    int extent = orientation == ADACHI_FRAME_COLUMNS ? parent->width : parent->height;

    if (extent < 2*min_size) return;

    float shift = (float)delta/(float)extent;
    float min = (float)min_size/(float)extent;
    float ratio = parent->ratio + (f == parent->first ? shift : -shift);

    if (ratio < min) ratio = min;
    if (ratio > 1.0f - min) ratio = 1.0f - min;
    parent->ratio = ratio;
}

static void adachi_frame_layout(Adachi_Frame *f, int x, int y, int width, int height)
{
    f->x = x;
    f->y = y;
    f->width = width;
    f->height = height;
    if (f->first == NULL) return;
    if (f->orientation == ADACHI_FRAME_COLUMNS) {
        int first = (int)((float)width*f->ratio);
        adachi_frame_layout(f->first, x, y, first, height);
        adachi_frame_layout(f->second, x + first, y, width - first, height);
    } else {
        int first = (int)((float)height*f->ratio);
        adachi_frame_layout(f->first, x, y, width, first);
        adachi_frame_layout(f->second, x, y + first, width, height - first);
    }
}

static void adachi_workspace_init(Adachi_Workspace *workspace)
{
    workspace->windows = (Adachi_Windows) {0};
    workspace->root_frame = adachi_frame_new();
    workspace->frame = workspace->root_frame;
}

static void adachi_workspace_free(Adachi_Workspace *workspace)
{
    adachi_frame_free(workspace->root_frame);
    free(workspace->windows.items);
    *workspace = (Adachi_Workspace) {0};
}

static void adachi_workspace_add_window(Adachi_Workspace *workspace, Window window)
{
    Adachi_Windows *ws = &workspace->windows;
    if (ws->count >= ws->capacity) {
        ws->capacity = ws->capacity == 0 ? 8 : ws->capacity*2;
        ws->items = realloc(ws->items, ws->capacity*sizeof(*ws->items));
        assert(ws->items != NULL && "Out of memory :<");
    }
    ws->items[ws->count++] = window;
}

static bool adachi_workspace_find_window(const Adachi_Workspace *workspace, Window window, size_t *index)
{
    for (size_t i = 0; i < workspace->windows.count; ++i) {
        if (workspace->windows.items[i] == window) {
            if (index != NULL) *index = i;
            return true;
        }
    }
    return false;
}

static bool adachi_workspace_remove_window(Adachi_Workspace *workspace, Window window)
{
    size_t index = 0;
    if (!adachi_workspace_find_window(workspace, window, &index)) return false;

    Adachi_Windows *ws = &workspace->windows;
    memmove(&ws->items[index], &ws->items[index + 1], (ws->count - index - 1)*sizeof(*ws->items));
    ws->count -= 1;
    if (workspace->last == window) workspace->last = None;

    Adachi_Frame *f = adachi_frame_find(workspace->root_frame, window);
    if (f == NULL) return true;

    Window next = workspace->last;
    if (next == None || adachi_frame_find(workspace->root_frame, next) != NULL) {
        next = adachi_workspace_hidden_from(workspace, index, true);
    }
    if (next == workspace->last) workspace->last = None;
    f->window = next;
    return true;
}

static Window adachi_workspace_hidden_from(const Adachi_Workspace *workspace, size_t index, bool forward)
{
    size_t count = workspace->windows.count;
    if (count == 0) return None;
    index %= count;
    for (size_t i = 0; i < count; ++i) {
        Window window = workspace->windows.items[index];
        if (adachi_frame_find(workspace->root_frame, window) == NULL) return window;
        index = forward ? (index + 1) % count : (index + count - 1) % count;
    }
    return None;
}

static Window adachi_workspace_hidden_after(const Adachi_Workspace *workspace, Window window, bool forward)
{
    size_t count = workspace->windows.count;
    if (count == 0) return None;
    size_t index = 0;
    if (adachi_workspace_find_window(workspace, window, &index)) {
        index = forward ? (index + 1) % count : (index + count - 1) % count;
    } else {
        index = forward ? 0 : count - 1;
    }
    return adachi_workspace_hidden_from(workspace, index, forward);
}

static void adachi_workspace_only(Adachi_Workspace *workspace)
{
    Adachi_Frame *old_root = workspace->root_frame;
    Adachi_Frame *only = adachi_frame_new();
    only->window = workspace->frame->window;
    workspace->root_frame = only;
    workspace->frame = only;
    adachi_frame_free(old_root);
}

#define ADACHI_RUN(command) command,
static const char *adachi_startup_commands[] = { ADACHI_STARTUP_COMMANDS NULL };
#undef ADACHI_RUN // ADACHI_RUN

static bool adachi_other_wm_running = false;

static void adachi_log_error(const char *fmt, ...)
{
    fprintf(stderr, "adachi: ERROR: ");
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");
}

static int adachi_wm_on_startup_error(Display *dpy, XErrorEvent *e)
{
    UNUSED(dpy);
    UNUSED(e);
    adachi_other_wm_running = true;
    return 0;
}

static int adachi_wm_on_error(Display *dpy, XErrorEvent *e)
{
    if (e->error_code == BadWindow) return 0;
    if (e->error_code == BadMatch && (e->request_code == X_SetInputFocus || e->request_code == X_ConfigureWindow)) return 0;

    char text[128] = {0};
    XGetErrorText(dpy, e->error_code, text, sizeof(text));
    adachi_log_error("X request %d: %s", e->request_code, text);
    return 0;
}

static int adachi_wm_window_size(int value)
{
    return value > 1 ? value : 1;
}

static void adachi_wm_hide_window(Adachi_WM *wm, Window window)
{
    XMoveWindow(wm->dpy, window, -2*wm->width, 0);
}

static void adachi_wm_apply_frame(Adachi_WM *wm, const Adachi_Frame *f)
{
    if (f->first != NULL) {
        adachi_wm_apply_frame(wm, f->first);
        adachi_wm_apply_frame(wm, f->second);
        return;
    }
    if (f->window == None) return;
    int border = ADACHI_BORDER_WIDTH;
    XSetWindowBorder(wm->dpy, f->window, f == wm->workspace->frame ? ADACHI_BORDER_FOCUS : ADACHI_BORDER_NORMAL);
    XMoveResizeWindow(wm->dpy, f->window, f->x, f->y,
                      (unsigned int)adachi_wm_window_size(f->width - 2*border),
                      (unsigned int)adachi_wm_window_size(f->height - 2*border));
}

static bool adachi_wm_supports_delete(const Adachi_WM *wm, Window w)
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

static void adachi_wm_refresh(Adachi_WM *wm)
{
    Adachi_Workspace *workspace = wm->workspace;
    for (size_t i = 0; i < workspace->windows.count; ++i) {
        Window window = workspace->windows.items[i];
        if (adachi_frame_find(workspace->root_frame, window) == NULL) adachi_wm_hide_window(wm, window);
    }
    adachi_frame_layout(workspace->root_frame, 0, 0, wm->width, wm->height);
    adachi_wm_apply_frame(wm, workspace->root_frame);

    Window w = workspace->frame->window;
    XSetInputFocus(wm->dpy, w != None ? w : wm->root, RevertToPointerRoot, CurrentTime);
}

static bool adachi_wm_grab_keyboard(Adachi_WM *wm)
{
    int status = XGrabKeyboard(wm->dpy, wm->root, False, GrabModeAsync, GrabModeAsync, CurrentTime);
    if (status == GrabSuccess) return true;
    adachi_log_error("Could not grab the keyboard (status %d)", status);
    return false;
}

static void adachi_wm_spawn(Adachi_WM *wm, const char *command)
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

static void adachi_wm_focus_frame(Adachi_WM *wm, Adachi_Frame *frame)
{
    Window old = wm->workspace->frame->window;
    wm->workspace->frame = frame;
    if (old != None && old != frame->window) wm->workspace->last = old;
    adachi_wm_refresh(wm);
}

static void adachi_wm_focus_direction(Adachi_WM *wm, int dx, int dy)
{
    Adachi_Frame *f = wm->workspace->frame;
    int x = f->x + f->width/2 + dx*(f->width/2 + 1);
    int y = f->y + f->height/2 + dy*(f->height/2 + 1);
    Adachi_Frame *target = adachi_frame_at(wm->workspace->root_frame, x, y);
    if (target != NULL) adachi_wm_focus_frame(wm, target);
}

static void adachi_wm_show_window(Adachi_WM *wm, Window window)
{
    Adachi_Frame *frame = adachi_frame_find(wm->workspace->root_frame, window);
    if (frame == NULL) {
        Window old = wm->workspace->frame->window;
        wm->workspace->frame->window = window;
        if (old != None && old != window) wm->workspace->last = old;
        adachi_wm_refresh(wm);
        return;
    }
    adachi_wm_focus_frame(wm, frame);
}

static void adachi_wm_show_last(Adachi_WM *wm)
{
    Window last = wm->workspace->last;
    if (last != None && adachi_workspace_find_window(wm->workspace, last, NULL)) adachi_wm_show_window(wm, last);
}

static void adachi_wm_cycle(Adachi_WM *wm, bool forward)
{
    Window window = adachi_workspace_hidden_after(wm->workspace, wm->workspace->frame->window, forward);
    if (window != None) adachi_wm_show_window(wm, window);
}

static void adachi_wm_close_focused(Adachi_WM *wm)
{
    Window w = wm->workspace->frame->window;
    if (w == None) return;
    if (!adachi_wm_supports_delete(wm, w)) {
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

static void adachi_wm_split(Adachi_WM *wm, Adachi_Frame_Orientation orientation)
{
    Adachi_Frame *f = wm->workspace->frame;
    int extent = orientation == ADACHI_FRAME_COLUMNS ? f->width : f->height;
    if (extent < 2*ADACHI_FRAME_MIN) return;
    Adachi_Frame *sibling = adachi_frame_split(&wm->workspace->root_frame, f, orientation);
    sibling->window = adachi_workspace_hidden_after(wm->workspace, f->window, true);
    adachi_wm_refresh(wm);
}

static void adachi_wm_only(Adachi_WM *wm)
{
    adachi_workspace_only(wm->workspace);
    adachi_wm_refresh(wm);
}

static void adachi_wm_remove_frame(Adachi_WM *wm)
{
    Adachi_Workspace *workspace = wm->workspace;
    Window old = workspace->frame->window;
    Adachi_Frame *target = adachi_frame_remove(&workspace->root_frame, workspace->frame);
    if (target == NULL) return;
    workspace->frame = target;
    if (old != None && old != target->window) workspace->last = old;
    adachi_wm_refresh(wm);
}

static void adachi_wm_adjust_frame(Adachi_WM *wm, int dx, int dy)
{
    adachi_frame_adjust_ratio(wm->workspace->frame, ADACHI_FRAME_COLUMNS, dx, ADACHI_FRAME_MIN);
    adachi_frame_adjust_ratio(wm->workspace->frame, ADACHI_FRAME_ROWS, dy, ADACHI_FRAME_MIN);
    adachi_wm_refresh(wm);
}

static void adachi_wm_begin_resize(Adachi_WM *wm)
{
    if (wm->workspace->root_frame->first == NULL) return;
    if (!adachi_wm_grab_keyboard(wm)) return;
    wm->resizing = true;
}

static void adachi_wm_switch_workspace(Adachi_WM *wm, Adachi_Workspace *workspace)
{
    if (workspace == wm->workspace) return;
    for (size_t i = 0; i < wm->workspace->windows.count; ++i) {
        adachi_wm_hide_window(wm, wm->workspace->windows.items[i]);
    }
    wm->workspace = workspace;
    adachi_wm_refresh(wm);
}

static void adachi_wm_move_to_workspace(Adachi_WM *wm, Adachi_Workspace *destination)
{
    Adachi_Workspace *source = wm->workspace;
    Window window = source->frame->window;
    if (window == None || destination == source) return;
    adachi_workspace_remove_window(source, window);
    adachi_workspace_add_window(destination, window);
    if (destination->frame->window == None) destination->frame->window = window;
    adachi_wm_hide_window(wm, window);
    adachi_wm_refresh(wm);
}

static Adachi_Workspace *adachi_wm_find_workspace(Adachi_WM *wm, Window window)
{
    for (size_t i = 0; i < wm->workspace_count; ++i) {
        if (adachi_workspace_find_window(&wm->workspaces[i], window, NULL)) return &wm->workspaces[i];
    }
    return NULL;
}

static void adachi_wm_manage(Adachi_WM *wm, Window window)
{
    Adachi_Workspace *workspace = adachi_wm_find_workspace(wm, window);
    if (workspace == NULL) {
        XSetWindowBorderWidth(wm->dpy, window, ADACHI_BORDER_WIDTH);
        adachi_workspace_add_window(wm->workspace, window);
    } else {
        adachi_wm_switch_workspace(wm, workspace);
    }
    XMapWindow(wm->dpy, window);
    adachi_wm_show_window(wm, window);
}

static void adachi_wm_unmanage(Adachi_WM *wm, Window window)
{
    Adachi_Workspace *workspace = adachi_wm_find_workspace(wm, window);
    if (workspace != NULL && adachi_workspace_remove_window(workspace, window) && workspace == wm->workspace) {
        adachi_wm_refresh(wm);
    }
}

static void adachi_wm_scan(Adachi_WM *wm)
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
        adachi_wm_manage(wm, children[i]);
    }
    if (children != NULL) XFree(children);
}

static void adachi_wm_on_configure_request(Adachi_WM *wm, XConfigureRequestEvent *e)
{
    if (adachi_wm_find_workspace(wm, e->window) != NULL) {
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
    *wm = (Adachi_WM) {0};

    wm->dpy = XOpenDisplay(NULL);
    if (wm->dpy == NULL) {
        adachi_log_error("Could not open the X display");
        return false;
    }

    int screen = DefaultScreen(wm->dpy);
    wm->root = RootWindow(wm->dpy, screen);
    wm->width = DisplayWidth(wm->dpy, screen);
    wm->height = DisplayHeight(wm->dpy, screen);
    wm->wm_protocols = XInternAtom(wm->dpy, "WM_PROTOCOLS", False);
    wm->wm_delete = XInternAtom(wm->dpy, "WM_DELETE_WINDOW", False);

    XSetErrorHandler(adachi_wm_on_startup_error);
    XSelectInput(wm->dpy, wm->root, SubstructureRedirectMask | SubstructureNotifyMask);
    XSync(wm->dpy, False);
    if (adachi_other_wm_running) {
        adachi_log_error("Another window manager is already running");
        XCloseDisplay(wm->dpy);
        wm->dpy = NULL;
        return false;
    }
    XSetErrorHandler(adachi_wm_on_error);

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = SIG_IGN;
    sa.sa_flags = SA_NOCLDSTOP | SA_NOCLDWAIT | SA_RESTART;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGCHLD, &sa, NULL);

    for (size_t i = 0; adachi_startup_commands[i] != NULL; ++i) {
        adachi_wm_spawn(wm, adachi_startup_commands[i]);
    }

    XDefineCursor(wm->dpy, wm->root, XCreateFontCursor(wm->dpy, XC_left_ptr));
    adachi_keys_grab_prefix(wm);

    wm->workspace_count = ADACHI_WORKSPACES;
    wm->workspaces = calloc(wm->workspace_count, sizeof(*wm->workspaces));
    assert(wm->workspaces != NULL);
    for (size_t i = 0; i < wm->workspace_count; ++i) {
        adachi_workspace_init(&wm->workspaces[i]);
    }
    wm->workspace = &wm->workspaces[0];

    adachi_wm_refresh(wm);
    adachi_wm_scan(wm);
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
        case MapRequest:       adachi_wm_manage(wm, e.xmaprequest.window); break;
        case UnmapNotify:      adachi_wm_unmanage(wm, e.xunmap.window); break;
        case DestroyNotify:    adachi_wm_unmanage(wm, e.xdestroywindow.window); break;
        case ConfigureRequest: adachi_wm_on_configure_request(wm, &e.xconfigurerequest); break;
        case KeyPress:         adachi_keys_on_press(wm, &e.xkey); break;
        default: break;
        }
    }
}

void adachi_wm_free(Adachi_WM *wm)
{
    for (size_t i = 0; i < wm->workspace_count; ++i) {
        adachi_workspace_free(&wm->workspaces[i]);
    }
    free(wm->workspaces);
    if (wm->dpy != NULL) XCloseDisplay(wm->dpy);
    *wm = (Adachi_WM) {0};
}

#define ADACHI_NUMLOCK_MASK Mod2Mask
#define ADACHI_MODS_MASK (ShiftMask | ControlMask | Mod1Mask | Mod4Mask)

typedef void Adachi_Action(Adachi_WM *wm, const char *arg);

typedef struct {
    KeySym key;
    Adachi_Action *action;
    const char *arg;
} Adachi_Binding;

#define ADACHI_ACTION(name, ...)                          \
    static void action_##name(Adachi_WM *wm, const char *arg) \
    {                                                     \
        UNUSED(arg);                                      \
        __VA_ARGS__;                                      \
    }

static void action_spawn(Adachi_WM *wm, const char *arg)
{
    adachi_wm_spawn(wm, arg);
}

ADACHI_ACTION(next,          adachi_wm_cycle(wm, true))
ADACHI_ACTION(prev,          adachi_wm_cycle(wm, false))
ADACHI_ACTION(other,         adachi_wm_show_last(wm))
ADACHI_ACTION(kill,          adachi_wm_close_focused(wm))
ADACHI_ACTION(quit,          wm->running = false)
ADACHI_ACTION(split_rows,    adachi_wm_split(wm, ADACHI_FRAME_ROWS))
ADACHI_ACTION(split_columns, adachi_wm_split(wm, ADACHI_FRAME_COLUMNS))
ADACHI_ACTION(frame_next,    adachi_wm_focus_frame(wm, adachi_frame_next(wm->workspace->frame)))
ADACHI_ACTION(frame_left,    adachi_wm_focus_direction(wm, -1, 0))
ADACHI_ACTION(frame_right,   adachi_wm_focus_direction(wm, 1, 0))
ADACHI_ACTION(frame_up,      adachi_wm_focus_direction(wm, 0, -1))
ADACHI_ACTION(frame_down,    adachi_wm_focus_direction(wm, 0, 1))
ADACHI_ACTION(resize,        adachi_wm_begin_resize(wm))
ADACHI_ACTION(only,          adachi_wm_only(wm))
ADACHI_ACTION(remove,        adachi_wm_remove_frame(wm))

#undef ADACHI_ACTION // ADACHI_ACTION

#define ADACHI_BIND(key, action, arg) { key, action, arg },
static const Adachi_Binding adachi_bindings[] = { ADACHI_BINDINGS };
#undef ADACHI_BIND // ADACHI_BIND

static void adachi_keys_grab_prefix(Adachi_WM *wm)
{
    KeyCode code = XKeysymToKeycode(wm->dpy, ADACHI_PREFIX_KEY);
    unsigned int locks[] = { 0, LockMask, ADACHI_NUMLOCK_MASK, LockMask | ADACHI_NUMLOCK_MASK };
    for (size_t i = 0; i < ARRAY_LEN(locks); ++i) {
        XGrabKey(wm->dpy, code, ADACHI_PREFIX_MOD | locks[i], wm->root, False, GrabModeAsync, GrabModeAsync);
    }
}

static bool adachi_keys_on_workspace_key(Adachi_WM *wm, XKeyEvent *e)
{
    unsigned int modifiers = e->state & ADACHI_MODS_MASK;
    if (modifiers != 0 && modifiers != ShiftMask) return false;

    KeySym key = XLookupKeysym(e, 0);
    if (key < XK_1 || key >= XK_1 + wm->workspace_count) return false;

    Adachi_Workspace *workspace = &wm->workspaces[key - XK_1];
    if (modifiers == ShiftMask) adachi_wm_move_to_workspace(wm, workspace);
    else adachi_wm_switch_workspace(wm, workspace);
    return true;
}

static void adachi_keys_on_resize_key(Adachi_WM *wm, KeySym key)
{
    if (IsModifierKey(key)) return;
    switch (key) {
    case XK_Left:  adachi_wm_adjust_frame(wm, -ADACHI_RESIZE_STEP, 0); break;
    case XK_Right: adachi_wm_adjust_frame(wm, ADACHI_RESIZE_STEP, 0); break;
    case XK_Up:    adachi_wm_adjust_frame(wm, 0, -ADACHI_RESIZE_STEP); break;
    case XK_Down:  adachi_wm_adjust_frame(wm, 0, ADACHI_RESIZE_STEP); break;
    default:
        XUngrabKeyboard(wm->dpy, CurrentTime);
        wm->resizing = false;
    }
}

static void adachi_keys_on_press(Adachi_WM *wm, XKeyEvent *e)
{
    KeySym key = XLookupKeysym(e, (e->state & ShiftMask) ? 1 : 0);

    if (wm->resizing) {
        adachi_keys_on_resize_key(wm, key);
        return;
    }

    if (!wm->waiting) {
        if (key != ADACHI_PREFIX_KEY || (e->state & ADACHI_MODS_MASK) != ADACHI_PREFIX_MOD) return;
        if (!adachi_wm_grab_keyboard(wm)) return;
        wm->waiting = true;
        return;
    }

    if (IsModifierKey(key)) return;
    XUngrabKeyboard(wm->dpy, CurrentTime);
    wm->waiting = false;

    if (adachi_keys_on_workspace_key(wm, e)) return;
    for (size_t i = 0; i < ARRAY_LEN(adachi_bindings); ++i) {
        if (adachi_bindings[i].key == key) {
            adachi_bindings[i].action(wm, adachi_bindings[i].arg);
            return;
        }
    }
}
