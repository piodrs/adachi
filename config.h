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

#define ADACHI_BINDINGS                                       \
    ADACHI_BIND(XK_c,      action_spawn,         "xterm")     \
    ADACHI_BIND(XK_exclam, action_spawn,         "dmenu_run") \
    ADACHI_BIND(XK_n,      action_next,          NULL)        \
    ADACHI_BIND(XK_space,  action_next,          NULL)        \
    ADACHI_BIND(XK_p,      action_prev,          NULL)        \
    ADACHI_BIND(XK_t,      action_other,         NULL)        \
    ADACHI_BIND(XK_k,      action_kill,          NULL)        \
    ADACHI_BIND(XK_q,      action_quit,          NULL)        \
    ADACHI_BIND(XK_s,      action_split_rows,    NULL)        \
    ADACHI_BIND(XK_S,      action_split_columns, NULL)        \
    ADACHI_BIND(XK_Tab,    action_frame_next,    NULL)        \
    ADACHI_BIND(XK_Left,   action_frame_left,    NULL)        \
    ADACHI_BIND(XK_Right,  action_frame_right,   NULL)        \
    ADACHI_BIND(XK_Up,     action_frame_up,      NULL)        \
    ADACHI_BIND(XK_Down,   action_frame_down,    NULL)        \
    ADACHI_BIND(XK_r,      action_resize,        NULL)        \
    ADACHI_BIND(XK_Q,      action_only,          NULL)        \
    ADACHI_BIND(XK_R,      action_remove,        NULL)

_Static_assert(ADACHI_WORKSPACES >= 1 && ADACHI_WORKSPACES <= 9, "ADACHI_WORKSPACES must be between 1 and 9");

#endif // ADACHI_CONFIG_H_
