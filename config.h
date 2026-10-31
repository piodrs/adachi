#ifndef ADACHI_CONFIG_H_
#define ADACHI_CONFIG_H_

#include <X11/keysym.h>
#include <X11/X.h>

#define ADACHI_PREFIX_KEY XK_t
#define ADACHI_PREFIX_MOD ControlMask

#define ADACHI_WORKSPACES 9

#define ADACHI_BORDER_WIDTH  1
#define ADACHI_BORDER_FOCUS  0x5f87af
#define ADACHI_BORDER_NORMAL 0x303030

#define ADACHI_FRAME_MIN   80
#define ADACHI_RESIZE_STEP 20

#define ADACHI_STARTUP_COMMANDS

#define ADACHI_BINDINGS                                   \
    ADACHI_BIND(XK_c,      wm_spawn,         "xterm")     \
    ADACHI_BIND(XK_exclam, wm_spawn,         "dmenu_run") \
    ADACHI_BIND(XK_n,      wm_next,          NULL)        \
    ADACHI_BIND(XK_space,  wm_next,          NULL)        \
    ADACHI_BIND(XK_p,      wm_prev,          NULL)        \
    ADACHI_BIND(XK_t,      wm_other,         NULL)        \
    ADACHI_BIND(XK_k,      wm_kill,          NULL)        \
    ADACHI_BIND(XK_q,      wm_quit,          NULL)        \
    ADACHI_BIND(XK_s,      wm_split_rows,    NULL)        \
    ADACHI_BIND(XK_S,      wm_split_columns, NULL)        \
    ADACHI_BIND(XK_Tab,    wm_frame_next,    NULL)        \
    ADACHI_BIND(XK_Left,   wm_frame_left,    NULL)        \
    ADACHI_BIND(XK_Right,  wm_frame_right,   NULL)        \
    ADACHI_BIND(XK_Up,     wm_frame_up,      NULL)        \
    ADACHI_BIND(XK_Down,   wm_frame_down,    NULL)        \
    ADACHI_BIND(XK_r,      wm_resize,        NULL)        \
    ADACHI_BIND(XK_Q,      wm_only,          NULL)        \
    ADACHI_BIND(XK_R,      wm_remove,        NULL)

_Static_assert(ADACHI_WORKSPACES >= 1 && ADACHI_WORKSPACES <= 9, "ADACHI_WORKSPACES must be between 1 and 9");

#endif // ADACHI_CONFIG_H_
