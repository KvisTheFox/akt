#include <stdio.h>
#include <stdint.h>
int main(void) {
    int64_t sum = 0;
    for (int64_t i = 0; i < 100000000LL; i++) sum += (i * 3 + 1) % 7;
    printf("loop %lld\n", (long long)sum);
    return 0;
}
