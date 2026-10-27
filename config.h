#ifndef ADACHI_CONFIG_H_
#define ADACHI_CONFIG_H_

#include <X11/keysym.h>
#include <X11/X.h>

#define ADACHI_PREFIX_KEY XK_t
#define ADACHI_PREFIX_MOD ControlMask

#define ADACHI_STARTUP_COMMANDS

#define ADACHI_BINDINGS                              \
    ADACHI_BIND(XK_c,      wm_exec,   "xterm")       \
    ADACHI_BIND(XK_exclam, wm_exec,   "dmenu_run")   \
    ADACHI_BIND(XK_n,      wm_next,   NULL)          \
    ADACHI_BIND(XK_space,  wm_next,   NULL)          \
    ADACHI_BIND(XK_p,      wm_prev,   NULL)          \
    ADACHI_BIND(XK_t,      wm_other,  NULL)          \
    ADACHI_BIND(XK_Tab,    wm_other,  NULL)          \
    ADACHI_BIND(XK_k,      wm_kill,   NULL)          \
    ADACHI_BIND(XK_q,      wm_quit,   NULL)          \
    ADACHI_BIND(XK_0,      wm_select, "0")           \
    ADACHI_BIND(XK_1,      wm_select, "1")           \
    ADACHI_BIND(XK_2,      wm_select, "2")           \
    ADACHI_BIND(XK_3,      wm_select, "3")           \
    ADACHI_BIND(XK_4,      wm_select, "4")           \
    ADACHI_BIND(XK_5,      wm_select, "5")           \
    ADACHI_BIND(XK_6,      wm_select, "6")           \
    ADACHI_BIND(XK_7,      wm_select, "7")           \
    ADACHI_BIND(XK_8,      wm_select, "8")           \
    ADACHI_BIND(XK_9,      wm_select, "9")

#endif // ADACHI_CONFIG_H_
