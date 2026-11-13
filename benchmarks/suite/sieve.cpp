#include <cstdio>
#include <cstdint>
#include <vector>
constexpr int64_t N = 5000000;
static uint8_t sieve[N];
int main() {
    for (int64_t i = 0; i < N; i++) sieve[i] = 1;
    sieve[0] = 0; sieve[1] = 0;
    for (int64_t i = 2; i * i < N; i++)
        if (sieve[i]) for (int64_t j = i * i; j < N; j += i) sieve[j] = 0;
    int64_t count = 0;
    for (int64_t k = 0; k < N; k++) count += sieve[k];
    std::printf("sieve %lld\n", (long long)count);
    return 0;
}
