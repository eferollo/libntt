#include <ntt/ntt.h>
#include <openssl/rand.h>
#include <stdint.h>
#include <stdlib.h>

int main(void)
{
    const uint32_t q = 113, n = 8, omega = 69, psi = 42;
    ntt_ctx *ctx = ntt_create(q, n, omega, psi);
    if (ctx == NULL) {
        fprintf(stderr, "ntt_create failed\n");
        return EXIT_FAILURE;
    }

    uint32_t a[8] = {99, 52, 0, 7, 0, 29, 33, 100},
             b[8] = {6, 0, 45, 12, 14, 78, 65, 112}, c[8];

    // for (uint32_t i = 0; i < n; i++) {
    //     if (RAND_bytes((unsigned char *)&a[i], sizeof(a[i])) != 1) {
    //         fprintf(stderr, "Failed to generate random number\n");
    //     }
    //     if (RAND_bytes((unsigned char *)&b[i], sizeof(b[i])) != 1) {
    //         fprintf(stderr, "Failed to generate random number\n");
    //     }
    // }

    int rc = ntt_negacyclic_mul(a, b, c, ctx);
    if (rc != NTT_OK) {
        ntt_destroy(ctx);
        ctx = NULL;
    }

    for (int i = 0; i < n; i++) {
        printf("%u ", c[i]);
    }

    return 0;
}
