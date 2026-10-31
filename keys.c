#include "keys.h"

#include <stddef.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#include "./thirdparty/axiom.h"
#include "config.h"

#define ADACHI_NUMLOCK_MASK Mod2Mask
#define ADACHI_MODS_MASK (ShiftMask | ControlMask | Mod1Mask | Mod4Mask)

typedef void Action(Adachi_WM *wm, const char *arg);

typedef struct {
    KeySym key;
    Action *action;
    const char *arg;
} Binding;

#define ACTION(name, ...)                          \
    static void action_##name(Adachi_WM *wm, const char *arg) \
    {                                               \
        AX_UNUSED(arg);                             \
        __VA_ARGS__;                                \
    }

static void action_spawn(Adachi_WM *wm, const char *arg)
{
    adachi_wm_spawn(wm, arg);
}

ACTION(next,          adachi_wm_cycle(wm, true))
ACTION(prev,          adachi_wm_cycle(wm, false))
ACTION(other,         adachi_wm_show_last(wm))
ACTION(kill,          adachi_wm_close_focused(wm))
ACTION(quit,          wm->running = false)
ACTION(split_rows,    adachi_wm_split(wm, ADACHI_FRAME_ROWS))
ACTION(split_columns, adachi_wm_split(wm, ADACHI_FRAME_COLUMNS))
ACTION(frame_next,    adachi_wm_focus_frame(wm, adachi_frame_next(wm->workspace->frame)))
ACTION(frame_left,    adachi_wm_focus_direction(wm, -1, 0))
ACTION(frame_right,   adachi_wm_focus_direction(wm, 1, 0))
ACTION(frame_up,      adachi_wm_focus_direction(wm, 0, -1))
ACTION(frame_down,    adachi_wm_focus_direction(wm, 0, 1))
ACTION(resize,        adachi_wm_begin_resize(wm))
ACTION(only,          adachi_wm_only(wm))
ACTION(remove,        adachi_wm_remove_frame(wm))

#undef ACTION

#define ADACHI_BIND(key, action, arg) { key, action, arg },
static const Binding bindings[] = { ADACHI_BINDINGS };
#undef ADACHI_BIND

void adachi_keys_grab_prefix(Adachi_WM *wm)
{
    KeyCode code = XKeysymToKeycode(wm->dpy, ADACHI_PREFIX_KEY);
    unsigned int locks[] = { 0, LockMask, ADACHI_NUMLOCK_MASK, LockMask | ADACHI_NUMLOCK_MASK };
    for (size_t i = 0; i < AX_ARRAY_LEN(locks); ++i) {
        XGrabKey(wm->dpy, code, ADACHI_PREFIX_MOD | locks[i], wm->root, False, GrabModeAsync, GrabModeAsync);
    }
}

static bool on_workspace_key(Adachi_WM *wm, XKeyEvent *e)
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

static void on_resize_key(Adachi_WM *wm, KeySym key)
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

void adachi_keys_on_press(Adachi_WM *wm, XKeyEvent *e)
{
    KeySym key = XLookupKeysym(e, (e->state & ShiftMask) ? 1 : 0);

    if (wm->resizing) {
        on_resize_key(wm, key);
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

    if (on_workspace_key(wm, e)) return;
    for (size_t i = 0; i < AX_ARRAY_LEN(bindings); ++i) {
        if (bindings[i].key == key) {
            bindings[i].action(wm, bindings[i].arg);
            return;
        }
    }
}
