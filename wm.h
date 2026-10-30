#ifndef ADACHI_WM_H_
#define ADACHI_WM_H_

#include <stdbool.h>
#include <X11/Xlib.h>

#include "./thirdparty/axiom.h"
#include "config.h"

typedef struct Adachi_Frame Adachi_Frame;

typedef struct {
    Ax_Array windows;
    Adachi_Frame *root_frame;
    Adachi_Frame *frame;
    Window last;
} Adachi_Workspace;

typedef struct {
    Display *dpy;
    Window root;

    int width;
    int height;

    Atom wm_protocols;
    Atom wm_delete;

    Adachi_Workspace workspaces[ADACHI_WORKSPACES];
    Adachi_Workspace *workspace;

    bool waiting;
    bool resizing;
    bool running;
} Adachi_WM;

bool adachi_wm_init(Adachi_WM *wm);
void adachi_wm_run(Adachi_WM *wm);
void adachi_wm_free(Adachi_WM *wm);

#endif // ADACHI_WM_H_
