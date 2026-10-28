#ifndef AXIOM_H_
#define AXIOM_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifndef AXDEF
#define AXDEF
#endif // AXDEF

#ifndef AX_ASSERT
#include <assert.h>
#define AX_ASSERT assert
#endif // AX_ASSERT

#if !defined(AX_REALLOC) || !defined(AX_FREE)
#include <stdlib.h>
#endif // !defined(AX_REALLOC) || !defined(AX_FREE)

#ifndef AX_REALLOC
#define AX_REALLOC realloc
#endif // AX_REALLOC

#ifndef AX_FREE
#define AX_FREE free
#endif // AX_FREE

#ifndef AX_ARRAY_START
#define AX_ARRAY_START 8
#endif // AX_ARRAY_START

#ifndef AX_ARENA_CAP
#define AX_ARENA_CAP (8*1024)
#endif // AX_ARENA_CAP

#define AX_UNUSED(x) ((void) (x))
#define AX_ARRAY_LEN(xs) (sizeof(xs)/sizeof((xs)[0]))

typedef struct {
    void *data;
    size_t length;
    size_t stride;
    size_t storage;
} Ax_Array;

#define ax_array(Type) ((Ax_Array) { .stride = sizeof(Type) })
#define ax_array_push_value(array, Type, value) (*(Type *) ax_array_push(array) = (value))
#define ax_array_at_as(array, index, Type) ((Type *) ax_array_at((array), (index)))

AXDEF void ax_array_reserve(Ax_Array *array, size_t length);
AXDEF void *ax_array_push(Ax_Array *array);
AXDEF void ax_array_write(Ax_Array *array, const void *items, size_t count);
AXDEF void *ax_array_at(const Ax_Array *array, size_t index);
AXDEF void ax_array_remove(Ax_Array *array, size_t index);
AXDEF void ax_array_release(Ax_Array *array);

AXDEF void ax_sb_append_cstr(Ax_Array *sb, const char *text);
AXDEF void ax_sb_appendf(Ax_Array *sb, const char *format, ...);
AXDEF char *ax_sb_cstr(Ax_Array *sb);

typedef struct Ax_Region Ax_Region;

struct Ax_Region {
    Ax_Region *next;
    size_t count;
    size_t capacity;
    uintptr_t data[];
};

typedef struct {
    Ax_Region *begin;
    Ax_Region *end;
} Ax_Arena;

typedef struct {
    Ax_Region *region;
    size_t count;
} Ax_Arena_Mark;

AXDEF void *ax_arena_alloc(Ax_Arena *arena, size_t size);
AXDEF char *ax_arena_strdup(Ax_Arena *arena, const char *text);
AXDEF Ax_Arena_Mark ax_arena_mark(const Ax_Arena *arena);
AXDEF void ax_arena_rewind(Ax_Arena *arena, Ax_Arena_Mark mark);
AXDEF void ax_arena_reset(Ax_Arena *arena);
AXDEF void ax_arena_free(Ax_Arena *arena);

typedef struct {
    size_t count;
    const char *data;
} Ax_String_View;

#define AX_SV_FMT "%.*s"
#define AX_SV_ARG(sv) (int) (sv).count, (sv).data
#define ax_sv_lit(text) ((Ax_String_View) { .count = sizeof(text) - 1, .data = (text) })

AXDEF Ax_String_View ax_sv_from_cstr(const char *text);
AXDEF Ax_String_View ax_sv_trim(Ax_String_View sv);
AXDEF Ax_String_View ax_sv_chop_left(Ax_String_View *sv, size_t count);
AXDEF Ax_String_View ax_sv_chop_by_delim(Ax_String_View *sv, char delim);
AXDEF bool ax_sv_chop_u64(Ax_String_View *sv, uint64_t *value);
AXDEF bool ax_sv_eq(Ax_String_View a, Ax_String_View b);
AXDEF bool ax_sv_starts_with(Ax_String_View sv, Ax_String_View prefix);
AXDEF bool ax_sv_ends_with(Ax_String_View sv, Ax_String_View suffix);

#ifdef AX_IMPLEMENTATION

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

AXDEF void ax_array_reserve(Ax_Array *array, size_t length)
{
    AX_ASSERT(array->stride > 0);
    if (length <= array->storage) return;
    size_t storage = array->storage != 0 ? array->storage : AX_ARRAY_START;
    while (storage < length) {
        AX_ASSERT(storage <= SIZE_MAX/2);
        storage *= 2;
    }
    AX_ASSERT(storage <= SIZE_MAX/array->stride);
    array->data = AX_REALLOC(array->data, storage*array->stride);
    AX_ASSERT(array->data != NULL && "Out of memory :<");
    array->storage = storage;
}

AXDEF void *ax_array_push(Ax_Array *array)
{
    ax_array_reserve(array, array->length + 1);
    void *slot = (unsigned char *) array->data + array->length*array->stride;
    array->length += 1;
    return slot;
}

AXDEF void ax_array_write(Ax_Array *array, const void *items, size_t count)
{
    if (count == 0) return;
    AX_ASSERT(count <= SIZE_MAX - array->length);
    ax_array_reserve(array, array->length + count);
    memcpy((unsigned char *) array->data + array->length*array->stride, items, count*array->stride);
    array->length += count;
}

AXDEF void *ax_array_at(const Ax_Array *array, size_t index)
{
    AX_ASSERT(index < array->length);
    return (unsigned char *) array->data + index*array->stride;
}

AXDEF void ax_array_remove(Ax_Array *array, size_t index)
{
    unsigned char *at = ax_array_at(array, index);
    memmove(at, at + array->stride, (array->length - index - 1)*array->stride);
    array->length -= 1;
}

AXDEF void ax_array_release(Ax_Array *array)
{
    AX_FREE(array->data);
    *array = (Ax_Array) {0};
}

AXDEF void ax_sb_append_cstr(Ax_Array *sb, const char *text)
{
    AX_ASSERT(sb->stride == sizeof(char));
    ax_array_write(sb, text, strlen(text));
}

AXDEF void ax_sb_appendf(Ax_Array *sb, const char *format, ...)
{
    AX_ASSERT(sb->stride == sizeof(char));
    va_list args;
    va_start(args, format);
    va_list copy;
    va_copy(copy, args);
    int n = vsnprintf(NULL, 0, format, copy);
    va_end(copy);
    AX_ASSERT(n >= 0);
    ax_array_reserve(sb, sb->length + (size_t) n + 1);
    vsnprintf((char *) sb->data + sb->length, (size_t) n + 1, format, args);
    sb->length += (size_t) n;
    va_end(args);
}

AXDEF char *ax_sb_cstr(Ax_Array *sb)
{
    AX_ASSERT(sb->stride == sizeof(char));
    ax_array_reserve(sb, sb->length + 1);
    ((char *) sb->data)[sb->length] = '\0';
    return sb->data;
}

AXDEF void *ax_arena_alloc(Ax_Arena *arena, size_t size)
{
    AX_ASSERT(size <= SIZE_MAX - sizeof(uintptr_t));
    size_t words = (size + sizeof(uintptr_t) - 1)/sizeof(uintptr_t);

    while (arena->end != NULL && arena->end->count + words > arena->end->capacity && arena->end->next != NULL) {
        arena->end = arena->end->next;
    }

    if (arena->end == NULL || arena->end->count + words > arena->end->capacity) {
        size_t capacity = words > AX_ARENA_CAP ? words : AX_ARENA_CAP;
        Ax_Region *region = AX_REALLOC(NULL, sizeof(*region) + capacity*sizeof(uintptr_t));
        AX_ASSERT(region != NULL && "Out of memory :<");
        *region = (Ax_Region) { .capacity = capacity };
        if (arena->end == NULL) arena->begin = region;
        else arena->end->next = region;
        arena->end = region;
    }

    void *result = arena->end->data + arena->end->count;
    arena->end->count += words;
    memset(result, 0, words*sizeof(uintptr_t));
    return result;
}

AXDEF char *ax_arena_strdup(Ax_Arena *arena, const char *text)
{
    size_t count = strlen(text) + 1;
    return memcpy(ax_arena_alloc(arena, count), text, count);
}

AXDEF Ax_Arena_Mark ax_arena_mark(const Ax_Arena *arena)
{
    return (Ax_Arena_Mark) {
        .region = arena->end,
        .count = arena->end != NULL ? arena->end->count : 0,
    };
}

AXDEF void ax_arena_rewind(Ax_Arena *arena, Ax_Arena_Mark mark)
{
    if (mark.region == NULL) {
        ax_arena_reset(arena);
        return;
    }
    mark.region->count = mark.count;
    for (Ax_Region *r = mark.region->next; r != NULL; r = r->next) r->count = 0;
    arena->end = mark.region;
}

AXDEF void ax_arena_reset(Ax_Arena *arena)
{
    for (Ax_Region *r = arena->begin; r != NULL; r = r->next) r->count = 0;
    arena->end = arena->begin;
}

AXDEF void ax_arena_free(Ax_Arena *arena)
{
    Ax_Region *r = arena->begin;
    while (r != NULL) {
        Ax_Region *next = r->next;
        AX_FREE(r);
        r = next;
    }
    *arena = (Ax_Arena) {0};
}

AXDEF Ax_String_View ax_sv_from_cstr(const char *text)
{
    return (Ax_String_View) { .count = strlen(text), .data = text };
}

AXDEF Ax_String_View ax_sv_trim(Ax_String_View sv)
{
    while (sv.count > 0 && isspace((unsigned char) sv.data[0])) ax_sv_chop_left(&sv, 1);
    while (sv.count > 0 && isspace((unsigned char) sv.data[sv.count - 1])) sv.count -= 1;
    return sv;
}

AXDEF Ax_String_View ax_sv_chop_left(Ax_String_View *sv, size_t count)
{
    if (count > sv->count) count = sv->count;
    Ax_String_View result = { .count = count, .data = sv->data };
    sv->data += count;
    sv->count -= count;
    return result;
}

AXDEF Ax_String_View ax_sv_chop_by_delim(Ax_String_View *sv, char delim)
{
    size_t i = 0;
    while (i < sv->count && sv->data[i] != delim) i += 1;
    Ax_String_View result = ax_sv_chop_left(sv, i);
    ax_sv_chop_left(sv, 1);
    return result;
}

AXDEF bool ax_sv_chop_u64(Ax_String_View *sv, uint64_t *value)
{
    uint64_t result = 0;
    size_t i = 0;
    while (i < sv->count && isdigit((unsigned char) sv->data[i])) {
        unsigned digit = (unsigned) (sv->data[i] - '0');
        if (result > (UINT64_MAX - digit)/10) return false;
        result = result*10 + digit;
        i += 1;
    }
    if (i == 0) return false;
    ax_sv_chop_left(sv, i);
    *value = result;
    return true;
}

AXDEF bool ax_sv_eq(Ax_String_View a, Ax_String_View b)
{
    return a.count == b.count && (a.count == 0 || memcmp(a.data, b.data, a.count) == 0);
}

AXDEF bool ax_sv_starts_with(Ax_String_View sv, Ax_String_View prefix)
{
    if (prefix.count > sv.count) return false;
    return ax_sv_eq(ax_sv_chop_left(&sv, prefix.count), prefix);
}

AXDEF bool ax_sv_ends_with(Ax_String_View sv, Ax_String_View suffix)
{
    if (suffix.count > sv.count) return false;
    ax_sv_chop_left(&sv, sv.count - suffix.count);
    return ax_sv_eq(sv, suffix);
}

#endif // AX_IMPLEMENTATION

#endif // AXIOM_H_
