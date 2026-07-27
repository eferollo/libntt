#include "ntt/ntt_adapter.h"
#include "ntt_adapter.h"
#include "ntt_internal.h"
#include <stdint.h>
#include <string.h>

ntt_ctx *ntt_create(const ntt_adapter *adapter, const ntt_config *config)
{
    ntt_ctx *ctx = NULL;
    uint32_t q, n, flags;

    if (adapter == NULL || config == NULL) {
        NTT_LOG(NTT_LOG_ERROR, "Invalid NTT adapter or configuration");
        return NULL;
    }

    if (ntt__adapter_is_compatible(adapter) == false) {
        /* Already verbose logging */
        return NULL;
    }

    if (adapter->validate_modulus == NULL || adapter->setup == NULL ||
        adapter->teardown == NULL) {
        NTT_LOG(NTT_LOG_ERROR, "Missing NTT adapter callbacks");
        return NULL;
    }

    q = ntt_config_get_modulus(config);
    if (adapter->validate_modulus(q) == false) {
        NTT_LOG(NTT_LOG_ERROR,
                "Unsupported modulus q=%u for adapter %s",
                q,
                adapter->name != NULL ? adapter->name : "<none>");
        return NULL;
    }

    n = ntt_config_get_size(config);
    if (ntt__is_power_of_two(n) == false) {
        NTT_LOG(NTT_LOG_ERROR, "n=%u is not a power of two", n);
        return NULL;
    }

    flags = ntt_config_get_flags(config);
    if (ntt_adapter_supports_flags(adapter, flags) == false) {
        NTT_LOG(NTT_LOG_ERROR,
                "Unsupported configuration flags 0x%x for adapter %s",
                flags & ~adapter->supported_flags,
                adapter->name != NULL ? adapter->name : "<none>");
        return NULL;
    }

    /*
     * Derive/validate omega and psi generically, before any adapter ever
     * sees the config. This is deliberately common code, not per-adapter:
     * the math (which roots exist, how they relate) doesn't depend on the
     * arithmetic representation an adapter chooses, only the transform
     * itself does. A local copy is resolved so the caller's ntt_config
     * object is never mutated as a side effect of ntt_create().
     */
    ntt_config resolved = *config;
    if (ntt__resolve_roots(q,
                           n,
                           ntt_config_get_transform_type(config),
                           &resolved.omega,
                           &resolved.psi) == false) {
        NTT_LOG(NTT_LOG_ERROR,
                "Failed to resolve omega/psi for q=%u n=%u",
                q,
                n);
        return NULL;
    }

    ctx = calloc(1, sizeof(*ctx));
    if (ctx == NULL) {
        NTT_LOG(NTT_LOG_ERROR, "ctx allocation failed");
        return NULL;
    }

    ctx->q = q;
    ctx->n = n;
    ctx->adapter = adapter;

    /*
     * All transform parameters, arithmetic state, and precomputed tables are
     * initialized by the selected adapter. The common layer deliberately does
     * not inspect or manipulate the opaque adapter state.
     */
    ctx->state = adapter->setup(&resolved);
    if (ctx->state == NULL) {
        NTT_LOG(NTT_LOG_ERROR,
                "Setup failed for adapter %s",
                adapter->name != NULL ? adapter->name : "<none>");
        goto cleanup;
    }

    NTT_LOG(NTT_LOG_INFO,
            "New NTT context: adapter=%s, q=%u n=%u omega=%u psi=%u flags=0x%x",
            adapter->name != NULL ? adapter->name : "<none>",
            q,
            n,
            resolved.omega,
            resolved.psi,
            flags);
    return ctx;

cleanup:
    ZERO_STRUCTP(ctx);
    SAFE_FREE(ctx);
    return NULL;
}

void ntt_destroy(ntt_ctx *ctx)
{
    if (ctx == NULL) {
        return;
    }

    NTT_LOG(NTT_LOG_DEBUG,
            "NTT context adapter=%s q=%u n=%u freed",
            ctx->adapter != NULL && ctx->adapter->name != NULL
                ? ctx->adapter->name
                : "<none>",
            ctx->q,
            ctx->n);

    if (ctx->adapter != NULL && ctx->adapter->teardown != NULL) {
        ctx->adapter->teardown(ctx->state);
    }

    ZERO_STRUCTP(ctx);
    SAFE_FREE(ctx);
}
