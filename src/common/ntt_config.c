#include "ntt_internal.h"
#include "string.h"

ntt_config *ntt_config_new(void)
{
    ntt_config *x = calloc(1, sizeof(struct ntt_config_s));
    if (x == NULL) {
        NTT_LOG(NTT_LOG_ERROR,
                "Error while allocating a new NTT configuration object");
    }
    return x;
}

void ntt_config_free(ntt_config *config)
{
    if (config == NULL) {
        NTT_LOG(NTT_LOG_DEBUG, "Invalid argument");
        return;
    }

    ZERO_STRUCTP(config);
    SAFE_FREE(config);
}

/* ntt_config setters */

int ntt_config_set_transform_type(ntt_config *config, ntt_transform_type type)
{
    if (config == NULL) {
        NTT_LOG(NTT_LOG_DEBUG, "Invalid argument");
        return NTT_ERROR;
    }
    config->transform_type = type;
    return NTT_OK;
}

int ntt_config_set_modulus(ntt_config *config, uint32_t q)
{
    if (config == NULL) {
        NTT_LOG(NTT_LOG_DEBUG, "Invalid argument");
        return NTT_ERROR;
    }
    config->q = q;
    return NTT_OK;
}

int ntt_config_set_size(ntt_config *config, uint32_t n)
{
    if (config == NULL) {
        NTT_LOG(NTT_LOG_DEBUG, "Invalid argument");
        return NTT_ERROR;
    }
    config->n = n;
    return NTT_OK;
}

int ntt_config_set_omega(ntt_config *config, uint32_t omega)
{
    if (config == NULL) {
        NTT_LOG(NTT_LOG_DEBUG, "Invalid argument");
        return NTT_ERROR;
    }
    config->omega = omega;
    return NTT_OK;
}

int ntt_config_set_psi(ntt_config *config, uint32_t psi)
{
    if (config == NULL) {
        NTT_LOG(NTT_LOG_DEBUG, "Invalid argument");
        return NTT_ERROR;
    }
    config->psi = psi;
    return NTT_OK;
}

int ntt_config_set_flags(ntt_config *config, uint32_t flags)
{
    if (config == NULL) {
        NTT_LOG(NTT_LOG_DEBUG, "Invalid argument");
        return NTT_ERROR;
    }
    config->flags = flags;
    return NTT_OK;
}

/* ntt_config getters */

ntt_transform_type ntt_config_get_transform_type(const ntt_config *config)
{
    if (config == NULL) {
        NTT_LOG(NTT_LOG_DEBUG, "Invalid argument");
        return NTT_TRANSFORM_NEGACYCLIC;
    }
    return config->transform_type;
}

uint32_t ntt_config_get_modulus(const ntt_config *config)
{
    if (config == NULL) {
        NTT_LOG(NTT_LOG_DEBUG, "Invalid argument");
        return 0;
    }
    return config->q;
}

uint32_t ntt_config_get_size(const ntt_config *config)
{
    if (config == NULL) {
        NTT_LOG(NTT_LOG_DEBUG, "Invalid argument");
        return 0;
    }
    return config->n;
}

uint32_t ntt_config_get_omega(const ntt_config *config)
{
    if (config == NULL) {
        NTT_LOG(NTT_LOG_DEBUG, "Invalid argument");
        return 0;
    }
    return config->omega;
}

uint32_t ntt_config_get_psi(const ntt_config *config)
{
    if (config == NULL) {
        NTT_LOG(NTT_LOG_DEBUG, "Invalid argument");
        return 0;
    }
    return config->psi;
}

uint32_t ntt_config_get_flags(const ntt_config *config)
{
    if (config == NULL) {
        NTT_LOG(NTT_LOG_DEBUG, "Invalid argument");
        return 0;
    }
    return config->flags;
}
