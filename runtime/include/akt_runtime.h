#ifndef AKT_RUNTIME_H
#define AKT_RUNTIME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__clang__) || defined(__GNUC__)
#define AKT_LIKELY(x) __builtin_expect(!!(x), 1)
#define AKT_UNLIKELY(x) __builtin_expect(!!(x), 0)
#define AKT_RESTRICT __restrict__
#define AKT_HOT __attribute__((hot))
#else
#define AKT_LIKELY(x) (x)
#define AKT_UNLIKELY(x) (x)
#define AKT_RESTRICT
#define AKT_HOT
#endif

typedef int8_t   akt_i8;
typedef int16_t  akt_i16;
typedef int32_t  akt_i32;
typedef int64_t  akt_i64;
typedef uint8_t  akt_u8;
typedef uint16_t akt_u16;
typedef uint32_t akt_u32;
typedef uint64_t akt_u64;
typedef float    akt_f32;
typedef double   akt_f64;
typedef bool     akt_bool;
typedef char     akt_cchar;

typedef struct {
    const char *data;
    size_t len;
} AktString;

typedef struct {
    unsigned char *data;
    size_t len;
    size_t cap;
    size_t elem_size;
} AktVec;

typedef struct {
    bool is_some;
    void *value;
    size_t size;
} AktOption;

typedef struct {
    bool is_ok;
    void *value;
    size_t size;
} AktResult;

typedef struct {
    AktString key;
    void *value;
} AktMapEntry;

typedef struct {
    AktMapEntry *entries;
    size_t len;
    size_t cap;
    size_t value_size;
} AktMap;

typedef struct {
    AktVec items;
} AktSet;

typedef struct AktMutex AktMutex;
typedef struct AktAtomic AktAtomic;
typedef struct AktShared AktShared;

AktShared *akt_shared_new(size_t size, const void *value);
void      *akt_shared_get(AktShared *s);
void       akt_shared_ref(AktShared *s);
void       akt_shared_unref(AktShared *s);
void       akt_shared_weak_ref(AktShared *s);
void       akt_shared_weak_unref(AktShared *s);
int        akt_shared_try_get(AktShared *s, void *out, size_t size);

typedef enum {
    AKT_JSON_NULL,
    AKT_JSON_BOOL,
    AKT_JSON_NUMBER,
    AKT_JSON_STRING,
    AKT_JSON_ARRAY,
    AKT_JSON_OBJECT
} AktJsonType;

typedef struct AktJson {
    AktJsonType type;
    bool boolean;
    double number;
    AktString string;
    AktVec array;
    AktVec keys;
    AktVec values;
} AktJson;

AktJson   akt_json_null(void);
AktJson   akt_json_parse(AktString text);
AktString akt_json_stringify(AktJson value);
AktJson   akt_json_get(AktJson value, AktString key);
AktJson   akt_json_at(AktJson value, size_t index);
size_t    akt_json_len(AktJson value);
AktString akt_json_to_string(AktJson value);
int       akt_regex_match(AktString pattern, AktString text);
AktString akt_regex_find(AktString pattern, AktString text);
AktString akt_regex_replace(AktString pattern, AktString text, AktString repl);
AktString akt_http_get(AktString url);
AktString akt_http_post(AktString url, AktString body);

typedef struct AktSqlite AktSqlite;
AktSqlite *akt_sqlite_open(AktString path);
int        akt_sqlite_exec(AktSqlite *db, AktString sql);
AktVec     akt_sqlite_query(AktSqlite *db, AktString sql);
void       akt_sqlite_close(AktSqlite *db);

AktMutex *akt_mutex_new(void);
void      akt_mutex_free(AktMutex *m);
void      akt_mutex_lock(AktMutex *m);
void      akt_mutex_unlock(AktMutex *m);

typedef void (*AktThreadFn)(void *);
typedef struct AktThread AktThread;
AktThread *akt_thread_spawn(AktThreadFn fn, void *arg);
void       akt_thread_join(AktThread *t);
void       akt_thread_sleep_ms(akt_i64 ms);

typedef struct AktTask AktTask;
typedef void *(*AktTaskFn)(void *);
AktTask *akt_task_start(AktTaskFn fn, void *arg);
void    *akt_task_wait(AktTask *t);
void    *akt_task_wait_timeout(AktTask *t, akt_i64 ms);
int      akt_task_wait_any(AktTask **tasks, int count);
void     akt_task_free(AktTask *t);

AktAtomic *akt_atomic_new(akt_i64 value);
void       akt_atomic_free(AktAtomic *a);
akt_i64    akt_atomic_load(AktAtomic *a);
void       akt_atomic_store(AktAtomic *a, akt_i64 value);
akt_i64    akt_atomic_add(AktAtomic *a, akt_i64 value);

AktSet    akt_set_new(size_t elem_size);
void      akt_set_free(AktSet *s);
void      akt_set_add(AktSet *s, const void *value);
int       akt_set_contains(const AktSet *s, const void *value);
void      akt_set_remove(AktSet *s, const void *value);
size_t    akt_set_len(const AktSet *s);

AktMap    akt_map_new(size_t value_size);
void      akt_map_free(AktMap *m);
void      akt_map_set(AktMap *m, AktString key, const void *value);
AktOption akt_map_get(const AktMap *m, AktString key);
int       akt_map_contains(const AktMap *m, AktString key);
void      akt_map_remove(AktMap *m, AktString key);
size_t    akt_map_len(const AktMap *m);

AktOption akt_option_some(const void *value, size_t size);
AktOption akt_option_none(void);
AktResult akt_result_ok(const void *value, size_t size);
AktResult akt_result_err(const void *value, size_t size);
AktResult akt_result_ok_unit(void);

#define AKT_OPTION_SOME(x) ({ __typeof__(x) _akt_v = (x); akt_option_some(&_akt_v, sizeof(_akt_v)); })
#define AKT_OPTION_NONE() akt_option_none()
#define AKT_RESULT_OK(x) ({ __typeof__(x) _akt_v = (x); akt_result_ok(&_akt_v, sizeof(_akt_v)); })
#define AKT_RESULT_ERR(x) ({ __typeof__(x) _akt_v = (x); akt_result_err(&_akt_v, sizeof(_akt_v)); })
#define AKT_RESULT_OK_UNIT() akt_result_ok_unit()

typedef struct AktArenaBlock AktArenaBlock;
typedef struct {
    AktArenaBlock *head;
    size_t block_size;
} AktArena;

/* Memory */
void *akt_alloc(size_t size);
void *akt_calloc(size_t count, size_t size);
void *akt_realloc(void *ptr, size_t size);
void  akt_free(void *ptr);
void  akt_runtime_shutdown(void);

AktArena akt_arena_new(size_t block_size);
void    *akt_arena_alloc(AktArena *arena, size_t size);
char    *akt_arena_strdup(AktArena *arena, const char *s);
void     akt_arena_free(AktArena *arena);

/* Strings */
AktString akt_str(const char *data, size_t len);
AktString akt_str_cstr(const char *data);
AktString akt_str_from_int(akt_i64 value);
AktString akt_str_from_float(akt_f64 value);
AktString akt_str_from_float_prec(akt_f64 value, int prec);
AktString akt_str_from_bool(akt_bool value);
AktString akt_str_from_char(uint32_t value);
AktString akt_str_concat(AktString a, AktString b);
AktString akt_str_slice(AktString s, size_t start, size_t end);
int       akt_str_eq(AktString a, AktString b);
int       akt_str_cmp(AktString a, AktString b);
size_t    akt_str_len(AktString s);
uint32_t  akt_str_char_at(AktString s, size_t index);
akt_i64   akt_str_to_int(AktString s, int *ok);
akt_f64   akt_str_to_float(AktString s, int *ok);
AktString akt_str_upper(AktString s);
AktString akt_str_lower(AktString s);
AktString akt_str_trim(AktString s);
AktString akt_str_reverse(AktString s);
AktString akt_str_capitalize(AktString s);
AktString akt_str_replace(AktString s, AktString from, AktString to);
AktString akt_str_repeat(AktString s, akt_i64 count);
int       akt_str_contains(AktString s, AktString needle);
int       akt_str_starts_with(AktString s, AktString prefix);
int       akt_str_ends_with(AktString s, AktString suffix);
akt_i64   akt_str_find(AktString s, AktString needle);
AktVec    akt_str_split(AktString s, AktString sep);
AktString akt_str_pad_left(AktString s, akt_i64 width, uint32_t ch);
AktString akt_str_pad_right(AktString s, akt_i64 width, uint32_t ch);
akt_i64   akt_str_parse_int(AktString s);
akt_f64   akt_str_parse_float(AktString s);

/* Paths */
AktString akt_path_join(AktString a, AktString b);
AktString akt_path_basename(AktString p);
AktString akt_path_dirname(AktString p);
AktString akt_path_extension(AktString p);
AktString akt_path_absolute(AktString p);

/* Printing */
AktString akt_str_slice(AktString s, size_t start, size_t end);
int       akt_str_eq(AktString a, AktString b);
int       akt_str_cmp(AktString a, AktString b);
size_t    akt_str_len(AktString s);
uint32_t  akt_str_char_at(AktString s, size_t index);
akt_i64   akt_str_to_int(AktString s, int *ok);
akt_f64   akt_str_to_float(AktString s, int *ok);

/* Printing */
void akt_print_str(AktString s);
void akt_println_str(AktString s);
void akt_print_int(akt_i64 v);
void akt_println_int(akt_i64 v);
void akt_print_float(akt_f64 v);
void akt_println_float(akt_f64 v);
void akt_print_bool(akt_bool v);
void akt_println_bool(akt_bool v);
void akt_print_char(uint32_t v);
void akt_println_char(uint32_t v);
void akt_print_newline(void);

/* Vectors */
AktVec akt_vec_new(size_t elem_size);
void   akt_vec_free(AktVec *v);
void   akt_vec_reserve(AktVec *v, size_t need);
void   akt_vec_push(AktVec *v, const void *elem);
void   akt_vec_push_front(AktVec *v, const void *elem);
void  *akt_vec_get(const AktVec *v, size_t index);
void  *akt_vec_front(const AktVec *v);
void  *akt_vec_back(const AktVec *v);
void   akt_vec_set(AktVec *v, size_t index, const void *elem);
void   akt_vec_pop(AktVec *v, void *out);
size_t akt_vec_len(const AktVec *v);
void   akt_vec_clear(AktVec *v);

/* Files and system helpers */
AktString akt_read_file(const char *path, int *ok);
int       akt_write_file(const char *path, AktString data);
int       akt_file_exists(const char *path);
AktVec    akt_fs_list_dir(AktString path);
AktVec    akt_fs_walk(AktString path);
AktString akt_env_get(const char *name, int *ok);
AktString akt_get_cwd(void);
akt_i64   akt_now_ms(void);
void      akt_sleep_ms(akt_i64 ms);
akt_i64   akt_process_pid(void);
void      akt_process_exit(int code);
akt_i64   akt_random_int(akt_i64 max);
akt_f64   akt_random_float(void);
akt_u64   akt_hash_fnv1a(AktString s);
AktString akt_base64_encode(AktString s);
AktString akt_base64_decode(AktString s);

/* Diagnostics */
void akt_panic(const char *message);
void akt_assert_fail(const char *expr, const char *file, int line);

#ifdef __cplusplus
}
#endif

#endif
