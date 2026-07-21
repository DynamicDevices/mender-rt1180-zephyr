// Copyright 2026 Dynamic Devices Ltd
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

/*
 * wifi_sim.c - Simulated Wi-Fi management interface for native_sim.
 *
 * Zephyr has no emulated Wi-Fi radio, so real association cannot be simulated.
 * This driver registers a management-only Wi-Fi interface so the pinned
 * improv-zephyr module can run its full provisioning handshake under native_sim
 * (scan / connect / state). It carries no data of its own: when a provisioning
 * "connect" arrives it reports association success and starts DHCPv4 on the real
 * native TAP Ethernet interface (zeth), which is where the Mender client's
 * traffic actually flows.
 *
 * The result is a genuine end-to-end loop in emulation:
 *   Improv (serial) provisioning -> network comes up over TAP -> Mender connects.
 *
 * What is NOT simulated: the radio association itself, IW612 SDIO enumeration,
 * firmware-blob load and real scan/associate/DHCP. Those remain hardware-only.
 */

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_l2.h>
#include <zephyr/net/ethernet.h>
#include <zephyr/net/dhcpv4.h>
#include <zephyr/net/wifi.h>
#include <zephyr/net/wifi_mgmt.h>

LOG_MODULE_REGISTER(wifi_sim, LOG_LEVEL_INF);

/* Deliver the connect result asynchronously, like a real driver, to avoid
 * re-entrancy in the net_mgmt caller and to mimic association latency.
 */
#define WIFI_SIM_ASSOC_DELAY K_MSEC(200)

/* Init after the native Ethernet driver so zeth is registered first. */
#define WIFI_SIM_INIT_PRIORITY 90

struct wifi_sim_data {
    struct net_if *iface;
    uint8_t        mac[6];
    bool           connected;
};

static struct wifi_sim_data wifi_sim;

/* Locate the real data interface: the first Ethernet interface that is not this
 * simulated Wi-Fi interface (i.e. native_sim's TAP-backed zeth).
 */
static void
find_data_iface_cb(struct net_if *iface, void *user_data) {
    struct net_if **out = user_data;

    if (NULL != *out) {
        return;
    }
    if (iface == wifi_sim.iface) {
        return;
    }
    if (net_if_l2(iface) != &NET_L2_GET_NAME(ETHERNET)) {
        return;
    }
    *out = iface;
}

static struct net_if *
data_iface(void) {
    struct net_if *iface = NULL;

    net_if_foreach(find_data_iface_cb, &iface);
    return iface;
}

static void
assoc_work_handler(struct k_work *work) {
    ARG_UNUSED(work);

    struct net_if *data = data_iface();

    wifi_sim.connected = true;
    wifi_mgmt_raise_connect_result_event(wifi_sim.iface, 0);

    if (NULL != data) {
        /* Route Mender traffic over the real TAP interface. */
        net_if_set_default(data);
        LOG_INF("provisioned; starting DHCPv4 on %s", net_if_get_device(data)->name);
        net_dhcpv4_start(data);
    } else {
        LOG_WRN("no data interface found for DHCP");
    }
}

static K_WORK_DELAYABLE_DEFINE(assoc_work, assoc_work_handler);

static int
wifi_sim_scan(const struct device *dev, struct wifi_scan_params *params, scan_result_cb_t cb) {
    ARG_UNUSED(dev);
    ARG_UNUSED(params);

    static const char ssid[] = "native-sim";
    struct wifi_scan_result res = {
        .ssid_length = sizeof(ssid) - 1,
        .rssi        = -42,
        .channel     = 1,
        .band        = WIFI_FREQ_BAND_2_4_GHZ,
        .security    = WIFI_SECURITY_TYPE_PSK,
    };

    memcpy(res.ssid, ssid, sizeof(ssid) - 1);

    /* One canned network, then a NULL entry to terminate the scan. */
    cb(wifi_sim.iface, 0, &res);
    cb(wifi_sim.iface, 0, NULL);

    return 0;
}

static int
wifi_sim_connect(const struct device *dev, struct wifi_connect_req_params *params) {
    ARG_UNUSED(dev);

    LOG_INF("connect request for SSID '%.*s'", params->ssid_length, (const char *)params->ssid);

    k_work_reschedule(&assoc_work, WIFI_SIM_ASSOC_DELAY);

    return 0;
}

static int
wifi_sim_disconnect(const struct device *dev) {
    ARG_UNUSED(dev);

    wifi_sim.connected = false;
    wifi_mgmt_raise_disconnect_result_event(wifi_sim.iface, 0);

    return 0;
}

static int
wifi_sim_iface_status(const struct device *dev, struct wifi_iface_status *status) {
    ARG_UNUSED(dev);

    status->state      = wifi_sim.connected ? WIFI_STATE_COMPLETED : WIFI_STATE_DISCONNECTED;
    status->iface_mode = WIFI_MODE_INFRA;
    status->link_mode  = WIFI_LINK_MODE_UNKNOWN;
    status->band       = WIFI_FREQ_BAND_2_4_GHZ;
    status->security   = WIFI_SECURITY_TYPE_NONE;
    status->mfp        = WIFI_MFP_DISABLE;

    return 0;
}

static const struct wifi_mgmt_ops wifi_sim_mgmt_ops = {
    .scan         = wifi_sim_scan,
    .connect      = wifi_sim_connect,
    .disconnect   = wifi_sim_disconnect,
    .iface_status = wifi_sim_iface_status,
};

static int
wifi_sim_send(const struct device *dev, struct net_pkt *pkt) {
    ARG_UNUSED(dev);
    ARG_UNUSED(pkt);

    /* This interface never carries data; the TAP interface does. */
    return -ENOTSUP;
}

static enum ethernet_hw_caps
wifi_sim_get_caps(const struct device *dev) {
    ARG_UNUSED(dev);
    return 0;
}

static void
wifi_sim_iface_init(struct net_if *iface) {
    struct ethernet_context *eth_ctx = net_if_l2_data(iface);

    wifi_sim.iface = iface;

    /* Stable locally-administered MAC (RFC 7042 documentation range). */
    wifi_sim.mac[0] = 0x02;
    wifi_sim.mac[1] = 0x00;
    wifi_sim.mac[2] = 0x5e;
    wifi_sim.mac[3] = 0x00;
    wifi_sim.mac[4] = 0x53;
    wifi_sim.mac[5] = 0x01;

    net_if_set_link_addr(iface, wifi_sim.mac, sizeof(wifi_sim.mac), NET_LINK_ETHERNET);

    eth_ctx->eth_if_type = L2_ETH_IF_TYPE_WIFI;
    ethernet_init(iface);
}

static int
wifi_sim_dev_init(const struct device *dev) {
    ARG_UNUSED(dev);
    return 0;
}

static struct net_wifi_mgmt_offload wifi_sim_api = {
    .wifi_iface.iface_api.init  = wifi_sim_iface_init,
    .wifi_iface.send            = wifi_sim_send,
    .wifi_iface.get_capabilities = wifi_sim_get_caps,
    .wifi_mgmt_api              = &wifi_sim_mgmt_ops,
};

ETH_NET_DEVICE_INIT(wifi_sim,
                    "wifi_sim",
                    wifi_sim_dev_init,
                    NULL,
                    &wifi_sim,
                    NULL,
                    WIFI_SIM_INIT_PRIORITY,
                    &wifi_sim_api,
                    NET_ETH_MTU);
