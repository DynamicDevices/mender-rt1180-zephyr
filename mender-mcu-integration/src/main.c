// Copyright 2024 Northern.tech AS
//
//    Licensed under the Apache License, Version 2.0 (the "License");
//    you may not use this file except in compliance with the License.
//    You may obtain a copy of the License at
//
//        http://www.apache.org/licenses/LICENSE-2.0
//
//    Unless required by applicable law or agreed to in writing, software
//    distributed under the License is distributed on an "AS IS" BASIS,
//    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//    See the License for the specific language governing permissions and
//    limitations under the License.

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(mender_app, LOG_LEVEL_DBG);

#include "utils/callbacks.h"
#include "utils/netup.h"
#include "utils/certs.h"

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/reboot.h>

#include <mender/utils.h>
#include <mender/client.h>
#include <mender/inventory.h>

#ifdef BUILD_INTEGRATION_TESTS
#include "modules/test-update-module.h"
#include "test_definitions.h"
#endif /* BUILD_INTEGRATION_TESTS */

#ifdef CONFIG_MENDER_ZEPHYR_IMAGE_UPDATE_MODULE
#include <mender/zephyr-image-update-module.h>
#endif /* CONFIG_MENDER_ZEPHYR_IMAGE_UPDATE_MODULE */

#ifdef CONFIG_MENDER_APP_NOOP_UPDATE_MODULE
#include "modules/noop-update-module.h"
#endif /* CONFIG_MENDER_APP_NOOP_UPDATE_MODULE */

#if defined(CONFIG_IMPROV_WIFI)
#include <improv/improv_wifi.h>
#endif

#if defined(CONFIG_APP_EINK)
#include "eink_display.h"
#include "eink_power.h"
#include "eink_scheduler.h"
#include "eink_store.h"
#if defined(CONFIG_APP_EINK_BATTERY_DUTY_CYCLE)
#include "eink_wake.h"
#endif
#if defined(CONFIG_APP_EINK_OTA_FLASH_STAGING)
#include "eink_ota_stage.h"
#endif
#if defined(CONFIG_APP_EINK_HTTP)
#include "eink_http.h"
#endif
#if defined(CONFIG_APP_EINK_SELFTEST)
#include "eink_selftest.h"
#endif
#endif

#ifdef CONFIG_MENDER_CLIENT_INVENTORY_DISABLE
#error Mender MCU integration app requires the inventory feature
#endif /* CONFIG_MENDER_CLIENT_INVENTORY_DISABLE */

MENDER_FUNC_WEAK mender_err_t
mender_network_connect_cb(void) {
#if defined(CONFIG_APP_EINK)
	(void)eink_power_iw612_set(true);
#endif
	LOG_DBG("network_connect_cb");
	return MENDER_OK;
}

MENDER_FUNC_WEAK mender_err_t
mender_network_release_cb(void) {
#if defined(CONFIG_APP_EINK)
	(void)eink_power_iw612_set(false);
#endif
	LOG_DBG("network_release_cb");
	return MENDER_OK;
}

MENDER_FUNC_WEAK mender_err_t
mender_deployment_status_cb(mender_deployment_status_t status, const char *desc) {
    LOG_DBG("deployment_status_cb: %s", desc);
    return MENDER_OK;
}

MENDER_FUNC_WEAK mender_err_t
mender_restart_cb(void) {
    LOG_DBG("restart_cb");

    sys_reboot(SYS_REBOOT_WARM);

    return MENDER_OK;
}

static char              mac_address[18] = { 0 };
static mender_identity_t mender_identity = { .name = "mac", .value = mac_address };

MENDER_FUNC_WEAK mender_err_t
mender_get_identity_cb(const mender_identity_t **identity) {
    LOG_DBG("get_identity_cb");
    if (NULL != identity) {
        *identity = &mender_identity;
        return MENDER_OK;
    }
    return MENDER_FAIL;
}

static mender_err_t
persistent_inventory_cb(mender_keystore_t **keystore, uint8_t *keystore_len) {
    static mender_keystore_t inventory[] = { { .name = "App", .value = "mender-mcu-integration" } };
    *keystore                            = inventory;
    *keystore_len                        = 1;
    return MENDER_OK;
}

int
main(void) {
    printf("Hello World! %s\n", CONFIG_BOARD_TARGET);

#if defined(CONFIG_APP_EINK)
    if (0 != eink_power_init()) {
        LOG_ERR("eink power policy failed (CM4 must stay held in reset)");
        goto END;
    }
    if (0 != eink_store_init(CONFIG_APP_EINK_STORE_ROOT)) {
        LOG_ERR("eink store init failed");
    }
#if defined(CONFIG_APP_EINK_OTA_FLASH_STAGING)
    if (0 != eink_ota_stage_init()) {
        LOG_WRN("ota staging init failed");
    }
#endif
    if (0 != eink_display_init()) {
        LOG_ERR("eink display init failed");
        goto END;
    }
    if (0 != eink_scheduler_init()) {
        LOG_ERR("eink scheduler init failed");
    }
#if defined(CONFIG_APP_EINK_HTTP)
    {
        struct eink_http_config hcfg = { 0 };

        strncpy(hcfg.api_base, CONFIG_APP_EINK_HTTP_BASE_URL, sizeof(hcfg.api_base) - 1);
        strncpy(hcfg.device_id, CONFIG_APP_EINK_HTTP_DEVICE_ID, sizeof(hcfg.device_id) - 1);
        strncpy(hcfg.auth_token, CONFIG_APP_EINK_HTTP_AUTH_TOKEN, sizeof(hcfg.auth_token) - 1);
        hcfg.poll_interval_seconds = CONFIG_APP_EINK_HTTP_POLL_INTERVAL;
#if defined(CONFIG_MENDER_NET_CA_CERTIFICATE_TAG_PRIMARY)
        hcfg.tls_sec_tag = CONFIG_MENDER_NET_CA_CERTIFICATE_TAG_PRIMARY;
#endif
        hcfg.enabled = IS_ENABLED(CONFIG_APP_EINK_HTTP_ENABLE) && (hcfg.device_id[0] != '\0');
        if (!hcfg.enabled) {
            /* Keep client initialized for shell-driven sync/fixture use. */
            if (hcfg.api_base[0] == '\0') {
                strncpy(hcfg.api_base, "file:///tmp/eink-zephyr", sizeof(hcfg.api_base) - 1);
            }
            if (hcfg.device_id[0] == '\0') {
                strncpy(hcfg.device_id, "native-sim", sizeof(hcfg.device_id) - 1);
            }
        }
        (void)eink_http_init(&hcfg);
    }
#endif
#if defined(CONFIG_APP_EINK_SELFTEST)
    if (0 != eink_selftest_run()) {
        LOG_ERR("eink selftest failed");
        goto END;
    }
#endif
#if defined(CONFIG_APP_EINK_BATTERY_DUTY_CYCLE)
    /* One cold-boot transaction then SNVS — skip always-on Mender poll loop. */
    if (0 != certs_add_credentials()) {
        LOG_ERR("Failed to add TLS credentials");
        goto END;
    }
    (void)eink_wake_run_once();
    goto END;
#endif
#endif

#if defined(CONFIG_IMPROV_WIFI)
    /*
     * Improv loads persisted Wi-Fi credentials and starts the BLE/serial
     * provisioning transports before Mender waits for an IPv4 address.
     */
    if (0 != improv_wifi_init()) {
        LOG_ERR("Failed to initialize Improv Wi-Fi provisioning");
        goto END;
    }
#endif

    /* Register CA tags before any HTTPS (e-tabelone or Mender). */
    if (0 != certs_add_credentials()) {
        LOG_ERR("Failed to add TLS credentials");
        goto END;
    }

    netup_wait_for_network();

#if defined(CONFIG_APP_EINK_HTTP)
    if (IS_ENABLED(CONFIG_APP_EINK_HTTP_ENABLE)) {
        if (0 != eink_http_start()) {
            LOG_WRN("eink http sync not started");
        }
    }
#endif

    if (IS_ENABLED(CONFIG_APP_MENDER_CLIENT_ENABLE)) {
        netup_get_mac_address(mender_identity.value);

        /* Initialize mender-client */
        mender_client_config_t    mender_client_config    = { .device_type = CONFIG_MENDER_DEVICE_TYPE, .recommissioning = false };
        mender_client_callbacks_t mender_client_callbacks = { .network_connect        = mender_network_connect_cb,
                                                              .network_release        = mender_network_release_cb,
                                                              .deployment_status      = mender_deployment_status_cb,
                                                              .restart                = mender_restart_cb,
                                                              .get_identity           = mender_get_identity_cb,
                                                              .get_user_provided_keys = NULL };

        LOG_INF("Initializing Mender Client with:");
        LOG_INF("   Device type:   '%s'", mender_client_config.device_type);
        LOG_INF("   Identity:      '{\"%s\": \"%s\"}'", mender_identity.name, mender_identity.value);

        if (MENDER_OK != mender_client_init(&mender_client_config, &mender_client_callbacks)) {
            LOG_ERR("Failed to initialize the client");
            goto END;
        }
        LOG_INF("Mender client initialized");

#ifdef CONFIG_MENDER_ZEPHYR_IMAGE_UPDATE_MODULE
    if (MENDER_OK != mender_zephyr_image_register_update_module()) {
        LOG_ERR("Failed to register the zephyr-image Update Module");
        goto END;
    }
    LOG_INF("Update Module 'zephyr-image' initialized");
#endif /* CONFIG_MENDER_ZEPHYR_IMAGE_UPDATE_MODULE */

#ifdef CONFIG_MENDER_APP_NOOP_UPDATE_MODULE
    if (MENDER_OK != noop_update_module_register()) {
        LOG_ERR("Failed to register the noop Update Module");
        goto END;
    }
    LOG_INF("Update Module 'noop-update' initialized");
#endif /* CONFIG_MENDER_APP_NOOP_UPDATE_MODULE */

#ifdef BUILD_INTEGRATION_TESTS
    if (MENDER_OK != test_update_module_register()) {
        LOG_ERR("Failed to register the test Update Module");
        goto END;
    }
    LOG_INF("Update Module 'test-update' initialized");
#endif /* BUILD_INTEGRATION_TESTS */

        if (MENDER_OK != mender_inventory_add_callback(persistent_inventory_cb, true)) {
            LOG_ERR("Failed to add inventory callback");
            goto END;
        }
        LOG_INF("Mender inventory callback added");

        /* Finally activate mender client */
        if (MENDER_OK != mender_client_activate()) {
            LOG_ERR("Unable to activate the client");
            goto END;
        }
        LOG_INF("Mender client activated and running!");
    } else {
        LOG_INF("Mender client disabled for focused simulator profile");
    }

END:
    k_sleep(K_FOREVER);

    return 0;
}
