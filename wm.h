#ifndef ADACHI_WM_H_
#define ADACHI_WM_H_

#include <stdbool.h>
#include <stddef.h>
#include <X11/Xlib.h>

typedef struct {
    Display *dpy;
    Window root;

    int width;
    int height;

    Atom wm_protocols;
    Atom wm_delete;

    struct {
        Window *items;
        size_t count;
        size_t capacity;
    } windows;

    Window current;
    Window last;

    bool waiting;
    bool running;
} Adachi_WM;

bool adachi_wm_init(Adachi_WM *wm);
void adachi_wm_run(Adachi_WM *wm);
void adachi_wm_free(Adachi_WM *wm);

#endif // ADACHI_WM_H_
