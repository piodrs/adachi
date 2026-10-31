#include "frame.h"

#include <stdlib.h>

#include "./thirdparty/axiom.h"

static void replace(Adachi_Frame **root, Adachi_Frame *from, Adachi_Frame *to)
{
    to->parent = from->parent;
    if (from->parent == NULL) *root = to;
    else if (from->parent->first == from) from->parent->first = to;
    else from->parent->second = to;
}

Adachi_Frame *adachi_frame_new(void)
{
    Adachi_Frame *f = calloc(1, sizeof(*f));
    AX_ASSERT(f != NULL);
    return f;
}

void adachi_frame_free(Adachi_Frame *f)
{
    if (f == NULL) return;
    adachi_frame_free(f->first);
    adachi_frame_free(f->second);
    free(f);
}

Adachi_Frame *adachi_frame_leftmost(Adachi_Frame *f)
{
    while (f->first != NULL) f = f->first;
    return f;
}

Adachi_Frame *adachi_frame_next(Adachi_Frame *f)
{
    for (; f->parent != NULL; f = f->parent) {
        if (f == f->parent->first) return adachi_frame_leftmost(f->parent->second);
    }
    return adachi_frame_leftmost(f);
}

Adachi_Frame *adachi_frame_find(Adachi_Frame *f, Window w)
{
    if (f->first == NULL) return f->window == w ? f : NULL;
    Adachi_Frame *found = adachi_frame_find(f->first, w);
    return found != NULL ? found : adachi_frame_find(f->second, w);
}

Adachi_Frame *adachi_frame_at(Adachi_Frame *f, int x, int y)
{
    if (x < f->x || x >= f->x + f->width || y < f->y || y >= f->y + f->height) return NULL;
    if (f->first == NULL) return f;
    Adachi_Frame *hit = adachi_frame_at(f->first, x, y);
    return hit != NULL ? hit : adachi_frame_at(f->second, x, y);
}

Adachi_Frame *adachi_frame_split(Adachi_Frame **root, Adachi_Frame *f, Adachi_Frame_Orientation orientation)
{
    Adachi_Frame *parent = adachi_frame_new();
    Adachi_Frame *sibling = adachi_frame_new();
    replace(root, f, parent);
    parent->orientation = orientation;
    parent->ratio = 0.5f;
    parent->first = f;
    parent->second = sibling;
    f->parent = parent;
    sibling->parent = parent;
    return sibling;
}

Adachi_Frame *adachi_frame_remove(Adachi_Frame **root, Adachi_Frame *f)
{
    Adachi_Frame *parent = f->parent;
    if (parent == NULL) return NULL;
    Adachi_Frame *sibling = parent->first == f ? parent->second : parent->first;
    replace(root, parent, sibling);
    free(f);
    free(parent);
    return adachi_frame_leftmost(sibling);
}

void adachi_frame_adjust_ratio(Adachi_Frame *f, Adachi_Frame_Orientation orientation, int delta, int min_size)
{
    if (delta == 0) return;
    while (f->parent != NULL && f->parent->orientation != orientation) f = f->parent;
    Adachi_Frame *parent = f->parent;
    
    if (parent == NULL) return;
    int extent = orientation == ADACHI_FRAME_COLUMNS ? parent->width : parent->height;
    
    if (extent < 2*min_size) return;
    
    float shift = (float)delta/(float)extent;
    float min = (float)min_size/(float)extent;
    float ratio = parent->ratio + (f == parent->first ? shift : -shift);

    parent->ratio = ax_clampf(ratio, min, 1.0f - min);
}

void adachi_frame_layout(Adachi_Frame *f, int x, int y, int width, int height)
{
    f->x = x;
    f->y = y;
    f->width = width;
    f->height = height;
    if (f->first == NULL) return;
    if (f->orientation == ADACHI_FRAME_COLUMNS) {
        int first = (int)((float)width*f->ratio);
        adachi_frame_layout(f->first, x, y, first, height);
        adachi_frame_layout(f->second, x + first, y, width - first, height);
    } else {
        int first = (int)((float)height*f->ratio);
        adachi_frame_layout(f->first, x, y, width, first);
        adachi_frame_layout(f->second, x, y + first, width, height - first);
    }
}
