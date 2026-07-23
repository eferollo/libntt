#ifndef NTT_ADAPTER_INTERNAL_H
#define NTT_ADAPTER_INTERNAL_H

#include <ntt/ntt_adapter.h>
#include <stddef.h>

bool ntt__adapter_has_field(const ntt_adapter *adapter,
                            size_t offset,
                            size_t size);

bool ntt__adapter_is_compatible(const ntt_adapter *adapter);

#endif /* NTT_ADAPTER_INTERNAL_H */
