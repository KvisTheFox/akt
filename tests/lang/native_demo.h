#ifndef NATIVE_DEMO_H
#define NATIVE_DEMO_H

typedef struct native_pair {
    int a;
    int b;
} native_pair;

typedef unsigned long native_size_alias;

enum native_mode {
    NATIVE_OFF = 0,
    NATIVE_ON = 1
};

#define NATIVE_MAGIC 7

#endif
