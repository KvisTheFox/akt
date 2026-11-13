#include <stdio.h>
#include <stdint.h>
#define N 16777216
static uint8_t data[N];
int main(void) {
    for (int64_t i = 0; i < N; i++) data[i] = (uint8_t)((i * 31 + 7) % 251);
    uint64_t h = 1469598103934665603ULL;
    for (int64_t i = 0; i < N; i++) {
        h ^= (uint64_t)data[i];
        h *= 1099511628211ULL;
    }
    printf("hash %llu\n", (unsigned long long)(h % 1000000007ULL));
    return 0;
}
