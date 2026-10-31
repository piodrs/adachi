#include "workspace.h"

void adachi_workspace_init(Adachi_Workspace *workspace)
{
    workspace->windows = ax_array(Window);
    workspace->root_frame = adachi_frame_new();
    workspace->frame = workspace->root_frame;
}

void adachi_workspace_free(Adachi_Workspace *workspace)
{
    adachi_frame_free(workspace->root_frame);
    ax_array_release(&workspace->windows);
    *workspace = (Adachi_Workspace) {0};
}

void adachi_workspace_add_window(Adachi_Workspace *workspace, Window window)
{
    ax_array_push_value(&workspace->windows, Window, window);
}

bool adachi_workspace_find_window(const Adachi_Workspace *workspace, Window window, size_t *index)
{
    Window *windows = workspace->windows.data;
    for (size_t i = 0; i < workspace->windows.length; ++i) {
        if (windows[i] == window) {
            if (index != NULL) *index = i;
            return true;
        }
    }
    return false;
}

bool adachi_workspace_remove_window(Adachi_Workspace *workspace, Window window)
{
    size_t index = 0;
    if (!adachi_workspace_find_window(workspace, window, &index)) return false;
    ax_array_remove(&workspace->windows, index);
    if (workspace->last == window) workspace->last = None;

    Adachi_Frame *f = adachi_frame_find(workspace->root_frame, window);
    if (f == NULL) return true;

    Window next = workspace->last;
    if (next == None || adachi_frame_find(workspace->root_frame, next) != NULL) {
        next = adachi_workspace_hidden_from(workspace, index, true);
    }
    if (next == workspace->last) workspace->last = None;
    f->window = next;
    return true;
}

Window adachi_workspace_hidden_from(const Adachi_Workspace *workspace, size_t index, bool forward)
{
    size_t count = workspace->windows.length;
    if (count == 0) return None;
    index %= count;
    for (size_t i = 0; i < count; ++i) {
        Window window = *ax_array_at_as(&workspace->windows, index, Window);
        if (adachi_frame_find(workspace->root_frame, window) == NULL) return window;
        index = forward ? (index + 1) % count : (index + count - 1) % count;
    }
    return None;
}

Window adachi_workspace_hidden_after(const Adachi_Workspace *workspace, Window window, bool forward)
{
    size_t count = workspace->windows.length;
    if (count == 0) return None;
    size_t index = 0;
    if (adachi_workspace_find_window(workspace, window, &index)) {
        index = forward ? (index + 1) % count : (index + count - 1) % count;
    } else {
        index = forward ? 0 : count - 1;
    }
    return adachi_workspace_hidden_from(workspace, index, forward);
}

void adachi_workspace_only(Adachi_Workspace *workspace)
{
    Adachi_Frame *old_root = workspace->root_frame;
    Adachi_Frame *only = adachi_frame_new();
    only->window = workspace->frame->window;
    workspace->root_frame = only;
    workspace->frame = only;
    adachi_frame_free(old_root);
}
