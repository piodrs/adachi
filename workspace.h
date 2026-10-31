#ifndef ADACHI_WORKSPACE_H_
#define ADACHI_WORKSPACE_H_

#include <stdbool.h>
#include <stddef.h>
#include <X11/X.h>

#include "./thirdparty/axiom.h"
#include "frame.h"

typedef struct {
    Ax_Array windows;
    Adachi_Frame *root_frame;
    Adachi_Frame *frame;
    Window last;
} Adachi_Workspace;

void adachi_workspace_init(Adachi_Workspace *workspace);
void adachi_workspace_free(Adachi_Workspace *workspace);

void adachi_workspace_add_window(Adachi_Workspace *workspace, Window window);
bool adachi_workspace_find_window(const Adachi_Workspace *workspace, Window window, size_t *index);
bool adachi_workspace_remove_window(Adachi_Workspace *workspace, Window window);

Window adachi_workspace_hidden_from(const Adachi_Workspace *workspace, size_t index, bool forward);
Window adachi_workspace_hidden_after(const Adachi_Workspace *workspace, Window window, bool forward);
void adachi_workspace_only(Adachi_Workspace *workspace);

#endif // ADACHI_WORKSPACE_H_
