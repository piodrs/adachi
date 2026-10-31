#ifndef ADACHI_KEYS_H_
#define ADACHI_KEYS_H_

#include <X11/Xlib.h>

#include "wm.h"

void adachi_keys_grab_prefix(Adachi_WM *wm);
void adachi_keys_on_press(Adachi_WM *wm, XKeyEvent *e);

#endif // ADACHI_KEYS_H_
