/*
 * Cold-boot duty-cycle wake transaction.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef EINK_WAKE_H
#define EINK_WAKE_H

/**
 * Run one wake: restore state, optional offline display, optional single
 * network session (sync + Mender check), persist next alarm, enter SNVS.
 * Does not return on success when SNVS is available.
 */
int eink_wake_run_once(void);

#endif
