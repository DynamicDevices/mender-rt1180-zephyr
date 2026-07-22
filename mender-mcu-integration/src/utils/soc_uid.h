/*
 * Canonical device identity: SoC unique ID from Zephyr hwinfo, uppercase hex.
 * Matches Active ESL onboard board_id / BLE DIS Serial (0x2A25) format.
 * Identity only — never use as an authentication secret (CRA Annex I (d)).
 */

#ifndef MENDER_APP_SOC_UID_H
#define MENDER_APP_SOC_UID_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* RT11xx = 8 bytes (16 hex); RT118x = 16 bytes (32 hex); + NUL. */
#define SOC_UID_HEX_MAX 33

/**
 * Read hwinfo device id and format as uppercase hex with no separators.
 *
 * @param out     Destination buffer
 * @param out_len Size of @p out (must be >= 2 * id_len + 1)
 * @return 0 on success, negative errno on failure
 */
int soc_uid_get_hex(char *out, size_t out_len);

#ifdef __cplusplus
}
#endif

#endif /* MENDER_APP_SOC_UID_H */
