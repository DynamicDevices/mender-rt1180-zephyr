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
LOG_MODULE_DECLARE(mender_app, LOG_LEVEL_DBG);

/* This piece of code is heavily inspired by Zephyr OS project samples:
 * https://github.com/zephyrproject-rtos/zephyr/tree/v3.7.0/samples/net/dhcpv4_client
 * https://github.com/zephyrproject-rtos/zephyr/tree/v3.7.0/samples/net/cloud/tagoio_http_post
 *
 * The main entry point, netup_wait_for_network, will set up the callbacks for the network
 * management and wait for NET_EVENT_IPV4_ADDR_ADD event (iow, the device obtained an IP address.
 * If WIFI configuration is enabled, a CONNECT request is issued and it is assumed that obtaining
 * the IP address is managed somewhere else.
 * The wait from netup_wait_for_network for the described event is controlled with a semaphore. */

#include "netup.h"

#include <stdio.h>
#include <assert.h>

#include <zephyr/kernel.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/ethernet.h>
#if defined(CONFIG_NET_DHCPV4)
#include <zephyr/net/dhcpv4.h>
#endif
#if defined(CONFIG_WIFI)
#include <zephyr/net/wifi_mgmt.h>
#endif
#if defined(CONFIG_NET_DSA)
#include <zephyr/net/dsa_core.h>
#endif

static K_SEM_DEFINE(network_ready_sem, 0, 1);

static struct net_mgmt_event_callback mgmt_cb;

#if defined(CONFIG_WIFI) && !defined(CONFIG_IMPROV_WIFI)

static struct wifi_connect_req_params cnx_params = {
    .ssid        = CONFIG_MENDER_APP_WIFI_SSID,
    .ssid_length = strlen(CONFIG_MENDER_APP_WIFI_SSID),
    .psk         = CONFIG_MENDER_APP_WIFI_PSK,
    .psk_length  = strlen(CONFIG_MENDER_APP_WIFI_PSK),
    .channel     = 0,
    .security    = WIFI_SECURITY_TYPE_PSK,
};

static void
wifi_connect(struct net_if *iface) {
    int ret = 0;

    LOG_INF("Connecting to wireless network %s...", cnx_params.ssid);

    int nr_tries = 20;
    while (nr_tries-- > 0) {
        ret = net_mgmt(NET_REQUEST_WIFI_CONNECT, iface, &cnx_params, sizeof(struct wifi_connect_req_params));
        if (ret == 0) {
            break;
        }

        /* Let's wait few seconds to allow wifi device be on-line */
        LOG_ERR("Connect request failed %d. Waiting iface be up...\n", ret);
        k_msleep(500);
    }
}

#endif /* CONFIG_WIFI && !CONFIG_IMPROV_WIFI */

static void
event_handler(struct net_mgmt_event_callback *cb, uint64_t mgmt_event, struct net_if *iface) {
    int i = 0;

    if (mgmt_event != NET_EVENT_IPV4_ADDR_ADD) {
        return;
    }

    for (i = 0; i < NET_IF_MAX_IPV4_ADDR; i++) {
        char buf[NET_IPV4_ADDR_LEN];
        enum net_addr_type type = iface->config.ip.ipv4->unicast[i].ipv4.addr_type;

        /* Accept DHCP and static/manual addresses (native_sim NET_CONFIG). */
        if (type != NET_ADDR_DHCP && type != NET_ADDR_OVERRIDABLE &&
            type != NET_ADDR_AUTOCONF && type != NET_ADDR_MANUAL) {
            continue;
        }
        if (iface->config.ip.ipv4->unicast[i].ipv4.address.family != AF_INET) {
            continue;
        }

        LOG_INF("   Address[%d]: %s",
                net_if_get_by_iface(iface),
                net_addr_ntop(AF_INET, &iface->config.ip.ipv4->unicast[i].ipv4.address.in_addr, buf, sizeof(buf)));
        LOG_INF("    Subnet[%d]: %s", net_if_get_by_iface(iface), net_addr_ntop(AF_INET, &iface->config.ip.ipv4->unicast[i].netmask, buf, sizeof(buf)));
        LOG_INF("    Router[%d]: %s", net_if_get_by_iface(iface), net_addr_ntop(AF_INET, &iface->config.ip.ipv4->gw, buf, sizeof(buf)));
#if defined(CONFIG_NET_DHCPV4)
        if (type == NET_ADDR_DHCP) {
            LOG_INF("Lease time[%d]: %u seconds", net_if_get_by_iface(iface), iface->config.dhcpv4.lease_time);
        }
#endif
    }

    // Network is up \o/
    k_sem_give(&network_ready_sem);
}

#if defined(CONFIG_NET_DSA)
static bool iface_is_dsa_user_port(struct net_if *iface)
{
	enum ethernet_hw_caps caps = net_eth_get_hw_capabilities(iface);

	if (caps & ETHERNET_DSA_CONDUIT_PORT) {
		return false;
	}
	return (caps & ETHERNET_DSA_USER_PORT) != 0;
}

#if defined(CONFIG_NET_DHCPV4)
static struct net_mgmt_event_callback dsa_if_up_cb;

static void dsa_dhcp_on_up(struct net_mgmt_event_callback *cb, uint64_t event,
			   struct net_if *iface)
{
	ARG_UNUSED(cb);

	if (event != NET_EVENT_IF_UP) {
		return;
	}
	if (!iface_is_dsa_user_port(iface)) {
		return;
	}

	LOG_INF("DSA user port %d up — starting DHCPv4", net_if_get_by_iface(iface));
	net_dhcpv4_start(iface);
}

static void dsa_dhcp_boot_scan(struct net_if *iface, void *user_data)
{
	ARG_UNUSED(user_data);

	if (net_if_is_up(iface)) {
		dsa_dhcp_on_up(NULL, NET_EVENT_IF_UP, iface);
	}
}
#endif /* CONFIG_NET_DHCPV4 */
#endif /* CONFIG_NET_DSA */

int
netup_wait_for_network(void) {
    net_mgmt_init_event_callback(&mgmt_cb, event_handler, NET_EVENT_IPV4_ADDR_ADD);
    net_mgmt_add_event_callback(&mgmt_cb);

    /* Prefer the provisioned interface when both Wi-Fi and Ethernet exist. */
#if defined(CONFIG_WIFI)
    struct net_if *iface = net_if_get_first_wifi();
    if (NULL == iface) {
        iface = net_if_get_default();
    }
#else
    struct net_if *iface = net_if_get_default();
#endif
    LOG_INF("Using net interface %s, index=%d", net_if_get_device(iface)->name, net_if_get_by_iface(iface));

#if defined(CONFIG_WIFI)
#if !defined(CONFIG_IMPROV_WIFI)
    wifi_connect(iface);
#endif
#elif defined(CONFIG_NET_DSA) && defined(CONFIG_NET_DHCPV4)
    /* FRDM NETC: default iface is often the conduit. DHCP every DSA user port. */
    net_mgmt_init_event_callback(&dsa_if_up_cb, dsa_dhcp_on_up, NET_EVENT_IF_UP);
    net_mgmt_add_event_callback(&dsa_if_up_cb);
    net_if_foreach(dsa_dhcp_boot_scan, NULL);
#elif defined(CONFIG_NET_DHCPV4)
    /* For WIFI, it is expected that the dhcp client is started somehow by the network management.
    This is the case for example for ESP32-S3 with configuration option WIFI_STA_AUTO_DHCPV4 */
    net_dhcpv4_start(iface);
#endif

    /*
     * Improv may have connected using stored credentials before this callback
     * was registered. Register first, then check the current state so the
     * IPv4 event cannot be missed between the check and the wait.
     * Static NET_CONFIG addresses are also already present here.
     */
    if (NULL != net_if_ipv4_get_global_addr(iface, NET_ADDR_PREFERRED)) {
        char buf[NET_IPV4_ADDR_LEN];
        const struct in_addr *addr = net_if_ipv4_get_global_addr(iface, NET_ADDR_PREFERRED);

        LOG_INF("Network already has a preferred IPv4 address");
        LOG_INF("   Address[%d]: %s", net_if_get_by_iface(iface),
                net_addr_ntop(AF_INET, addr, buf, sizeof(buf)));
        return 0;
    }

    // Wait for network
    LOG_INF("Waiting for network up...");
    return k_sem_take(&network_ready_sem, K_FOREVER);
}

void
netup_get_mac_address(char *address) {
    assert(NULL != address);

    struct net_if *iface = net_if_get_first_up();
    assert(NULL != iface);

    struct net_linkaddr *linkaddr = net_if_get_link_addr(iface);
    assert(NULL != linkaddr);

    snprintf(address,
             18,
             "%02x:%02x:%02x:%02x:%02x:%02x",
             linkaddr->addr[0],
             linkaddr->addr[1],
             linkaddr->addr[2],
             linkaddr->addr[3],
             linkaddr->addr[4],
             linkaddr->addr[5]);
}
