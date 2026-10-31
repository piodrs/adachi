#define _POSIX_C_SOURCE 200809L

#define AX_IMPLEMENTATION
#include "./thirdparty/axiom.h"

#include "wm.h"

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

#include "config.h"
#include "keys.h"

#define ADACHI_RUN(command) command,
static const char *startup_commands[] = { ADACHI_STARTUP_COMMANDS NULL };
#undef ADACHI_RUN

static bool other_wm_running = false;

static void log_error(const char *fmt, ...)
{
    fprintf(stderr, "adachi: ERROR: ");
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");
}

static int on_startup_error(Display *dpy, XErrorEvent *e)
{
    AX_UNUSED(dpy);
    AX_UNUSED(e);
    other_wm_running = true;
    return 0;
}

static int on_error(Display *dpy, XErrorEvent *e)
{
    if (e->error_code == BadWindow) return 0;
    if (e->error_code == BadMatch && (e->request_code == X_SetInputFocus || e->request_code == X_ConfigureWindow)) return 0;

    char text[128] = {0};
    XGetErrorText(dpy, e->error_code, text, sizeof(text));
    log_error("X request %d: %s", e->request_code, text);
    return 0;
}

static int window_size(int value)
{
    return value > 1 ? value : 1;
}

static void hide_window(Adachi_WM *wm, Window window)
{
    XMoveWindow(wm->dpy, window, -2*wm->width, 0);
}

static void apply_frame(Adachi_WM *wm, const Adachi_Frame *f)
{
    if (f->first != NULL) {
        apply_frame(wm, f->first);
        apply_frame(wm, f->second);
        return;
    }
    if (f->window == None) return;
    int border = ADACHI_BORDER_WIDTH;
    XSetWindowBorder(wm->dpy, f->window, f == wm->workspace->frame ? ADACHI_BORDER_FOCUS : ADACHI_BORDER_NORMAL);
    XMoveResizeWindow(wm->dpy, f->window, f->x, f->y,
                      (unsigned int)window_size(f->width - 2*border),
                      (unsigned int)window_size(f->height - 2*border));
}

static bool supports_delete(const Adachi_WM *wm, Window w)
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

static void refresh(Adachi_WM *wm)
{
    Adachi_Workspace *workspace = wm->workspace;
    Window *windows = workspace->windows.data;
    for (size_t i = 0; i < workspace->windows.length; ++i) {
        if (adachi_frame_find(workspace->root_frame, windows[i]) == NULL) hide_window(wm, windows[i]);
    }
    adachi_frame_layout(workspace->root_frame, 0, 0, wm->width, wm->height);
    apply_frame(wm, workspace->root_frame);

    Window w = workspace->frame->window;
    XSetInputFocus(wm->dpy, w != None ? w : wm->root, RevertToPointerRoot, CurrentTime);
}

bool adachi_wm_grab_keyboard(Adachi_WM *wm)
{
    int status = XGrabKeyboard(wm->dpy, wm->root, False, GrabModeAsync, GrabModeAsync, CurrentTime);
    if (status == GrabSuccess) return true;
    log_error("Could not grab the keyboard (status %d)", status);
    return false;
}

void adachi_wm_spawn(Adachi_WM *wm, const char *command)
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

void adachi_wm_focus_frame(Adachi_WM *wm, Adachi_Frame *frame)
{
    Window old = wm->workspace->frame->window;
    wm->workspace->frame = frame;
    if (old != None && old != frame->window) wm->workspace->last = old;
    refresh(wm);
}

void adachi_wm_focus_direction(Adachi_WM *wm, int dx, int dy)
{
    Adachi_Frame *f = wm->workspace->frame;
    int x = f->x + f->width/2 + dx*(f->width/2 + 1);
    int y = f->y + f->height/2 + dy*(f->height/2 + 1);
    Adachi_Frame *target = adachi_frame_at(wm->workspace->root_frame, x, y);
    if (target != NULL) adachi_wm_focus_frame(wm, target);
}

static void show_window(Adachi_WM *wm, Window window)
{
    Adachi_Frame *frame = adachi_frame_find(wm->workspace->root_frame, window);
    if (frame == NULL) {
        Window old = wm->workspace->frame->window;
        wm->workspace->frame->window = window;
        if (old != None && old != window) wm->workspace->last = old;
        refresh(wm);
        return;
    }
    adachi_wm_focus_frame(wm, frame);
}

void adachi_wm_show_last(Adachi_WM *wm)
{
    Window last = wm->workspace->last;
    if (last != None && adachi_workspace_find_window(wm->workspace, last, NULL)) show_window(wm, last);
}

void adachi_wm_cycle(Adachi_WM *wm, bool forward)
{
    Window window = adachi_workspace_hidden_after(wm->workspace, wm->workspace->frame->window, forward);
    if (window != None) show_window(wm, window);
}

void adachi_wm_close_focused(Adachi_WM *wm)
{
    Window w = wm->workspace->frame->window;
    if (w == None) return;
    if (!supports_delete(wm, w)) {
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

void adachi_wm_split(Adachi_WM *wm, Adachi_Frame_Orientation orientation)
{
    Adachi_Frame *f = wm->workspace->frame;
    int extent = orientation == ADACHI_FRAME_COLUMNS ? f->width : f->height;
    if (extent < 2*ADACHI_FRAME_MIN) return;
    Adachi_Frame *sibling = adachi_frame_split(&wm->workspace->root_frame, f, orientation);
    sibling->window = adachi_workspace_hidden_after(wm->workspace, f->window, true);
    refresh(wm);
}

void adachi_wm_only(Adachi_WM *wm)
{
    adachi_workspace_only(wm->workspace);
    refresh(wm);
}

void adachi_wm_remove_frame(Adachi_WM *wm)
{
    Adachi_Workspace *workspace = wm->workspace;
    Window old = workspace->frame->window;
    Adachi_Frame *target = adachi_frame_remove(&workspace->root_frame, workspace->frame);
    if (target == NULL) return;
    workspace->frame = target;
    if (old != None && old != target->window) workspace->last = old;
    refresh(wm);
}

void adachi_wm_adjust_frame(Adachi_WM *wm, int dx, int dy)
{
    adachi_frame_adjust_ratio(wm->workspace->frame, ADACHI_FRAME_COLUMNS, dx, ADACHI_FRAME_MIN);
    adachi_frame_adjust_ratio(wm->workspace->frame, ADACHI_FRAME_ROWS, dy, ADACHI_FRAME_MIN);
    refresh(wm);
}

void adachi_wm_begin_resize(Adachi_WM *wm)
{
    if (wm->workspace->root_frame->first == NULL) return;
    if (!adachi_wm_grab_keyboard(wm)) return;
    wm->resizing = true;
}

void adachi_wm_switch_workspace(Adachi_WM *wm, Adachi_Workspace *workspace)
{
    if (workspace == wm->workspace) return;
    Window *windows = wm->workspace->windows.data;
    for (size_t i = 0; i < wm->workspace->windows.length; ++i) hide_window(wm, windows[i]);
    wm->workspace = workspace;
    refresh(wm);
}

void adachi_wm_move_to_workspace(Adachi_WM *wm, Adachi_Workspace *destination)
{
    Adachi_Workspace *source = wm->workspace;
    Window window = source->frame->window;
    if (window == None || destination == source) return;
    adachi_workspace_remove_window(source, window);
    adachi_workspace_add_window(destination, window);
    if (destination->frame->window == None) destination->frame->window = window;
    hide_window(wm, window);
    refresh(wm);
}

static Adachi_Workspace *find_workspace(Adachi_WM *wm, Window window)
{
    for (size_t i = 0; i < wm->workspace_count; ++i) {
        if (adachi_workspace_find_window(&wm->workspaces[i], window, NULL)) return &wm->workspaces[i];
    }
    return NULL;
}

static void manage(Adachi_WM *wm, Window window)
{
    Adachi_Workspace *workspace = find_workspace(wm, window);
    if (workspace == NULL) {
        XSetWindowBorderWidth(wm->dpy, window, ADACHI_BORDER_WIDTH);
        adachi_workspace_add_window(wm->workspace, window);
    } else {
        adachi_wm_switch_workspace(wm, workspace);
    }
    XMapWindow(wm->dpy, window);
    show_window(wm, window);
}

static void unmanage(Adachi_WM *wm, Window window)
{
    Adachi_Workspace *workspace = find_workspace(wm, window);
    if (workspace != NULL && adachi_workspace_remove_window(workspace, window) && workspace == wm->workspace) {
        refresh(wm);
    }
}

static void scan(Adachi_WM *wm)
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
        manage(wm, children[i]);
    }
    if (children != NULL) XFree(children);
}

static void on_configure_request(Adachi_WM *wm, XConfigureRequestEvent *e)
{
    if (find_workspace(wm, e->window) != NULL) {
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
        log_error("Could not open the X display");
        return false;
    }

    int screen = DefaultScreen(wm->dpy);
    wm->root = RootWindow(wm->dpy, screen);
    wm->width = DisplayWidth(wm->dpy, screen);
    wm->height = DisplayHeight(wm->dpy, screen);
    wm->wm_protocols = XInternAtom(wm->dpy, "WM_PROTOCOLS", False);
    wm->wm_delete = XInternAtom(wm->dpy, "WM_DELETE_WINDOW", False);

    XSetErrorHandler(on_startup_error);
    XSelectInput(wm->dpy, wm->root, SubstructureRedirectMask | SubstructureNotifyMask);
    XSync(wm->dpy, False);
    if (other_wm_running) {
        log_error("Another window manager is already running");
        XCloseDisplay(wm->dpy);
        wm->dpy = NULL;
        return false;
    }
    XSetErrorHandler(on_error);

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = SIG_IGN;
    sa.sa_flags = SA_NOCLDSTOP | SA_NOCLDWAIT | SA_RESTART;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGCHLD, &sa, NULL);

    for (size_t i = 0; startup_commands[i] != NULL; ++i) {
        adachi_wm_spawn(wm, startup_commands[i]);
    }

    XDefineCursor(wm->dpy, wm->root, XCreateFontCursor(wm->dpy, XC_left_ptr));
    adachi_keys_grab_prefix(wm);

    wm->workspace_count = ADACHI_WORKSPACES;
    wm->workspaces = calloc(wm->workspace_count, sizeof(*wm->workspaces));
    AX_ASSERT(wm->workspaces != NULL);
    for (size_t i = 0; i < wm->workspace_count; ++i) {
        adachi_workspace_init(&wm->workspaces[i]);
    }
    wm->workspace = &wm->workspaces[0];

    refresh(wm);
    scan(wm);
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
        case MapRequest:       manage(wm, e.xmaprequest.window); break;
        case UnmapNotify:      unmanage(wm, e.xunmap.window); break;
        case DestroyNotify:    unmanage(wm, e.xdestroywindow.window); break;
        case ConfigureRequest: on_configure_request(wm, &e.xconfigurerequest); break;
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
