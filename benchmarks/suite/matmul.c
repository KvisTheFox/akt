#include <stdio.h>
#define D 256
static double a[D * D], b[D * D], c[D * D];
int main(void) {
    for (int i = 0; i < D * D; i++) {
        a[i] = (double)(i % 13);
        b[i] = (double)(i % 7);
        c[i] = 0.0;
    }
    for (int r = 0; r < D; r++)
        for (int k = 0; k < D; k++) {
            double av = a[r * D + k];
            for (int col = 0; col < D; col++) c[r * D + col] += av * b[k * D + col];
        }
    double sum = 0.0;
    for (int i = 0; i < D * D; i++) sum += c[i];
    printf("matmul %.2f\n", sum);
    return 0;
}
