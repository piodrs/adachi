#include "wm.h"

int main(void)
{
    Adachi_WM wm = {0};
    if (!adachi_wm_init(&wm)) return 1;
    adachi_wm_run(&wm);
    adachi_wm_free(&wm);
    return 0;
}
