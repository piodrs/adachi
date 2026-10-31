#ifndef ADACHI_FRAME_H_
#define ADACHI_FRAME_H_

#include <X11/X.h>

typedef enum {
    ADACHI_FRAME_ROWS,
    ADACHI_FRAME_COLUMNS,
} Adachi_Frame_Orientation;

typedef struct Adachi_Frame Adachi_Frame;

struct Adachi_Frame {
    Adachi_Frame *parent;
    Adachi_Frame *first;
    Adachi_Frame *second;
    Adachi_Frame_Orientation orientation;
    float ratio;
    Window window;
    int x;
    int y;
    int width;
    int height;
};

Adachi_Frame *adachi_frame_new(void);
void adachi_frame_free(Adachi_Frame *f);

Adachi_Frame *adachi_frame_leftmost(Adachi_Frame *f);
Adachi_Frame *adachi_frame_next(Adachi_Frame *f);
Adachi_Frame *adachi_frame_find(Adachi_Frame *f, Window w);
Adachi_Frame *adachi_frame_at(Adachi_Frame *f, int x, int y);

Adachi_Frame *adachi_frame_split(Adachi_Frame **root, Adachi_Frame *f, Adachi_Frame_Orientation orientation);
Adachi_Frame *adachi_frame_remove(Adachi_Frame **root, Adachi_Frame *f);

void adachi_frame_adjust_ratio(Adachi_Frame *f, Adachi_Frame_Orientation orientation, int delta, int min_size);
void adachi_frame_layout(Adachi_Frame *f, int x, int y, int width, int height);

#endif // ADACHI_FRAME_H_
