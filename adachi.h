#ifndef ADACHI_H_
#define ADACHI_H_

#include <stdbool.h>
#include <stddef.h>
#include <X11/Xlib.h>

typedef enum {
    ADACHI_FRAME_ROWS,
    ADACHI_FRAME_COLUMNS,
} Adachi_Frame_Orientation;

typedef struct Adachi_Frame Adachi_Frame;

struct Adachi_Frame {
    Adachi_Frame *parent;
    Adachi_Frame *first;
    Adachi_Frame *second;
    Adachi_Frame_Orientation orientation;
    float ratio;
    Window window;
    int x;
    int y;
    int width;
    int height;
};

typedef struct {
    Window *items;
    size_t count;
    size_t capacity;
} Adachi_Windows;

typedef struct {
    Adachi_Windows windows;
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

#endif // ADACHI_H_
