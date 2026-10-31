#ifndef ADACHI_WM_H_
#define ADACHI_WM_H_

#include <stdbool.h>
#include <stddef.h>
#include <X11/Xlib.h>

#include "workspace.h"

typedef struct {
    Display *dpy;
    Window root;
    int width;
    int height;
    Atom wm_protocols;
    Atom wm_delete;
    Adachi_Workspace *workspaces;
    size_t workspace_count;
    Adachi_Workspace *workspace;
    bool waiting;
    bool resizing;
    bool running;
} Adachi_WM;

bool adachi_wm_init(Adachi_WM *wm);
void adachi_wm_run(Adachi_WM *wm);
void adachi_wm_free(Adachi_WM *wm);

bool adachi_wm_grab_keyboard(Adachi_WM *wm);
void adachi_wm_spawn(Adachi_WM *wm, const char *command);
void adachi_wm_focus_frame(Adachi_WM *wm, Adachi_Frame *frame);
void adachi_wm_focus_direction(Adachi_WM *wm, int dx, int dy);
void adachi_wm_show_last(Adachi_WM *wm);
void adachi_wm_cycle(Adachi_WM *wm, bool forward);
void adachi_wm_close_focused(Adachi_WM *wm);
void adachi_wm_split(Adachi_WM *wm, Adachi_Frame_Orientation orientation);
void adachi_wm_only(Adachi_WM *wm);
void adachi_wm_remove_frame(Adachi_WM *wm);
void adachi_wm_adjust_frame(Adachi_WM *wm, int dx, int dy);
void adachi_wm_begin_resize(Adachi_WM *wm);
void adachi_wm_switch_workspace(Adachi_WM *wm, Adachi_Workspace *workspace);
void adachi_wm_move_to_workspace(Adachi_WM *wm, Adachi_Workspace *destination);

#endif // ADACHI_WM_H_
