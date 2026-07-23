/*
 * WORK IN PROGRESS
 */

#include <ntt/ntt.h>
#include <openssl/rand.h>
#include <stdint.h>
#include <stdlib.h>

int main(void)
{
    const ntt_adapter *adapter = ntt_adapter_scalar_toy();
    ntt_config *config = ntt_config_new();
    if (config == NULL) {
        fprintf(stderr, "ntt_config_new failed\n");
        return EXIT_FAILURE;
    }

    ntt_config_set_modulus(config, 113);
    ntt_config_set_size(config, 8);
    ntt_config_set_omega(config, 69);
    ntt_config_set_psi(config, 42);

    ntt_ctx *ctx = ntt_create(adapter, config);
    ntt_config_free(config);
    if (ctx == NULL) {
        fprintf(stderr, "ntt_create failed\n");
        return EXIT_FAILURE;
    }

    uint32_t a[8] = {99, 52, 0, 7, 0, 29, 33, 100},
             b[8] = {6, 0, 45, 12, 14, 78, 65, 112}, c[8];

    int rc = ntt_negacyclic_mul(a, b, c, ctx);
    if (rc != NTT_OK) {
        ntt_destroy(ctx);
        return EXIT_FAILURE;
    }

    for (uint32_t i = 0; i < 8; i++) {
        printf("%u ", c[i]);
    }
    printf("\n");

    ntt_destroy(ctx);
    return EXIT_SUCCESS;
}
