#ifndef ADACHI_WM_H_
#define ADACHI_WM_H_

#include <stdbool.h>
#include <X11/Xlib.h>

#include "axiom.h"

typedef struct Adachi_Frame Adachi_Frame;

typedef struct {
    Display *dpy;
    Window root;

    int width;
    int height;

    Atom wm_protocols;
    Atom wm_delete;

    Ax_Array windows;

    Adachi_Frame *root_frame;
    Adachi_Frame *frame;
    Window last;

    bool waiting;
    bool resizing;
    bool running;
} Adachi_WM;

bool adachi_wm_init(Adachi_WM *wm);
void adachi_wm_run(Adachi_WM *wm);
void adachi_wm_free(Adachi_WM *wm);

#endif // ADACHI_WM_H_
