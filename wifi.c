#include <linux/module.h>

#include <net/cfg80211.h> /* wifi and probably everything that would required for FullMAC driver */
#include <linux/skbuff.h>

#include <linux/workqueue.h> /* work_struct */
#include <linux/semaphore.h>

#define WIFI_NAME "wifi_drv"
#define NDEV_NAME "wifi_drv%d"
#define SSID_DUMMY "WiFi"
#define SSID_DUMMY_SIZE (sizeof("WiFi") - 1)

struct wifi_drv_context {
    struct wifi *wifi;
    struct net_device *ndev;

    /* DEMO */
    struct semaphore sem;
    struct work_struct ws_connect;
    char connecting_ssid[sizeof(SSID_DUMMY)];
    struct work_struct ws_disconnect;
    u16 disconnect_reason_code;
    struct work_struct ws_scan;
    struct cfg80211_scan_request *scan_request;
};

struct wifi_drv_wifi_priv_context {
    struct wifi_drv_context *wifi_drv;
};

struct wifi_drv_ndev_priv_context {
    struct wifi_drv_context *wifi_drv;
    struct wireless_dev wdev;
};

/* helper function that will retrieve main context from "priv" data of the wifi */
static struct wifi_drv_wifi_priv_context *
wifi_get_wifi_drv_context(struct wifi *wifi) { return (struct wifi_drv_wifi_priv_context *) wifi_priv(wifi); }

/* helper function that will retrieve main context from "priv" data of the network device */
static struct wifi_drv_ndev_priv_context *
ndev_get_wifi_drv_context(struct net_device *ndev) { return (struct wifi_drv_ndev_priv_context *) netdev_priv(ndev); }

/* Helper function that will prepare structure with "dummy" BSS information and "inform" the kernel about "new" BSS */
static void inform_dummy_bss(struct wifi_drv_context *wifi_drv) {
    struct cfg80211_bss *bss = NULL;
    struct cfg80211_inform_bss data = {
            .chan = &wifi_drv->wifi->bands[NL80211_BAND_2GHZ]->channels[0], /* the only channel for this demo */
            .scan_width = NL80211_BSS_CHAN_WIDTH_20,
            /* signal "type" not specified in this DEMO so its basically unused, it can be some kind of percentage from 0 to 100 or mBm value*/
            /* signal "type" may be specified before wifi registration by setting wifi->signal_type */
            .signal = 1337,
    };
    char bssid[6] = {0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};

    /* ie - array of tags that usually retrieved from beacon frame or probe responce. */
    char ie[SSID_DUMMY_SIZE + 2] = {WLAN_EID_SSID, SSID_DUMMY_SIZE};
    memcpy(ie + 2, SSID_DUMMY, SSID_DUMMY_SIZE);

    /* also it posible to use cfg80211_inform_bss() instead of cfg80211_inform_bss_data() */
    bss = cfg80211_inform_bss_data(wifi_drv->wifi, &data, CFG80211_BSS_FTYPE_UNKNOWN, bssid, 0, WLAN_CAPABILITY_ESS, 100,
                                   ie, sizeof(ie), GFP_KERNEL);

    /* free, cfg80211_inform_bss_data() returning cfg80211_bss structure refcounter of which should be decremented if its not used. */
    cfg80211_put_bss(wifi_drv->wifi, bss);
}

/* "Scan" routine for DEMO. It just inform the kernel about "dummy" BSS and "finishs" scan.
 * When scan is done it should call cfg80211_scan_done() to inform the kernel that scan is finished.
 * This routine called through workqueue, when the kernel asks about scan through cfg80211_ops. */
static void wifi_drv_scan_routine(struct work_struct *w) {
    struct wifi_drv_context *wifi_drv = container_of(w, struct wifi_drv_context, ws_scan);
    struct cfg80211_scan_info info = {
            /* if scan was aborted by user(calling cfg80211_ops->abort_scan) or by any driver/hardware issue - field should be set to "true"*/
            .aborted = false,
    };

    /* pretend some work, also u can't call cfg80211_scan_done right away after cfg80211_ops->scan(),
     * idk why, but netlink client would not get message with "scan done",
     * is it because of "scan_routine" and cfg80211_ops->scan() may run in concurrent and cfg80211_scan_done() called before cfg80211_ops->scan() returns? */
    msleep(100);

    /* inform with dummy BSS */
    inform_dummy_bss(wifi_drv);

    if(down_interruptible(&wifi_drv->sem)) {
        return;
    }

    /* finish scan */
    cfg80211_scan_done(wifi_drv->scan_request, &info);

    wifi_drv->scan_request = NULL;

    up(&wifi_drv->sem);
}

/* It just checks SSID of the ESS to connect and informs the kernel that connect is finished.
 * It should call cfg80211_connect_bss() when connect is finished or cfg80211_connect_timeout() when connect is failed.
 * This "demo" can connect only to ESS with SSID equal to SSID_DUMMY value.
 * This routine called through workqueue, when the kernel asks about connect through cfg80211_ops. */
static void wifi_drv_connect_routine(struct work_struct *w) {
    struct wifi_drv_context *wifi_drv = container_of(w, struct wifi_drv_context, ws_connect);

    if(down_interruptible(&wifi_drv->sem)) {
        return;
    }

    if (memcmp(wifi_drv->connecting_ssid, SSID_DUMMY, sizeof(SSID_DUMMY)) != 0) {
        cfg80211_connect_timeout(wifi_drv->ndev, NULL, NULL, 0, GFP_KERNEL, NL80211_TIMEOUT_SCAN);
    } else {
        /* we can connect to ESS that already know. If else, technically kernel will only warn.*/
        /* so, lets send dummy bss to the kernel before complete. */
        inform_dummy_bss(wifi_drv);

        /* also its possible to use cfg80211_connect_result() or cfg80211_connect_done() */
        cfg80211_connect_bss(wifi_drv->ndev, NULL, NULL, NULL, 0, NULL, 0, WLAN_STATUS_SUCCESS, GFP_KERNEL,
                             NL80211_TIMEOUT_UNSPECIFIED);
    }
    wifi_drv->connecting_ssid[0] = 0;

    up(&wifi_drv->sem);
}

/* Just calls cfg80211_disconnected() that informs the kernel that disconnect is complete.
 * Overall disconnect may call cfg80211_connect_timeout() if disconnect interrupting connection routine, but for this demo I keep it simple.
 * This routine called through workqueue, when the kernel asks about disconnect through cfg80211_ops. */
static void wifi_drv_disconnect_routine(struct work_struct *w) {

    struct wifi_drv_context *wifi_drv = container_of(w, struct wifi_drv_context, ws_disconnect);

    if(down_interruptible(&wifi_drv->sem)) {
        return;
    }

    cfg80211_disconnected(wifi_drv->ndev, wifi_drv->disconnect_reason_code, NULL, 0, true, GFP_KERNEL);

    wifi_drv->disconnect_reason_code = 0;
    
    up(&wifi_drv->sem);
}

/* HostAP mode functions */
static int wifi_drv_start_ap(struct wifi *wifi, struct net_device *dev,
                           struct cfg80211_config_params *params) {
    struct wifi_drv_context *wifi_drv = wifi_get_wifi_drv_context(wifi)->wifi_drv;

    if (down_interruptible(&wifi_drv->sem)) {
        return -ERESTARTSYS;
    }

    // Set the AP mode in the driver context
    wifi_drv->ap_mode_enabled = true;

    // Perform any necessary initialization for AP mode, such as setting up beacon and DTIM parameters

    // Inform the kernel about the start of AP mode
    cfg80211_ap_start(wifi_drv->ndev, params);

    up(&wifi_drv->sem);

    return 0; /* OK */
}

static void wifi_drv_stop_ap(struct net_device *dev) {
    struct wifi_drv_context *wifi_drv = dev_priv(dev);
    struct cfg80211_ap_config ap_config;

    // Clear the device mode to station
    dev_set_mode(dev, NL80211_IFTYPE_STATION);

    // Unregister the AP configuration
    memset(&ap_config, 0, sizeof(ap_config));
    ap_config.ifindex = dev->ifindex;
    cfg80211_unregister_bss(wifi_drv->wifi, &ap_config);
}

static int nvf_add_virtual_intf(struct wiphy *wiphy, struct net_device *dev,
                                 enum nl80211_iftype type, const char *name,
                                 unsigned char name_assign_type) {
    struct wifi_drv_context *wifi_drv = wiphy_get_wifi_drv_context(wiphy)->wifi_drv;
    struct net_device *new_dev;

    // Allocate a new net_device for the virtual interface
    new_dev = alloc_netdev(sizeof(struct wifi_drv_context), name, NET_NAME_ENUM, ether_setup);
    if (!new_dev) {
        return -ENOMEM; // Memory allocation failed
    }

    // Set the interface type
    new_dev->ieee80211_ptr->iftype = type;

    // Set the parent wiphy
    new_dev->ieee80211_ptr->wiphy = wiphy;

    // Register the new virtual interface
    if (register_netdev(new_dev)) {
        free_netdev(new_dev);
        return -EIO; // Failed to register the device
    }

    // If the new interface is for AP mode, start the AP
    if (type == NL80211_IFTYPE_AP) {
        struct cfg80211_config_params params = { /* Initialize with necessary parameters */ };
        if (wifi_drv_start_ap(wifi_drv, new_dev, &params) < 0) {
            unregister_netdev(new_dev);
            free_netdev(new_dev);
            return -EIO; // Failed to start AP mode
        }
    }

    // Add the new device to the driver context
    // You may want to maintain a list of virtual interfaces in your driver context

    return 0; // Success
}


static int nvf_change_virtual_intf(struct wiphy *wiphy, struct net_device *dev,
                                    enum nl80211_iftype type) {
    struct wifi_drv_context *wifi_drv = dev_priv(dev);

    // Check if the requested type is valid
    if (type != NL80211_IFTYPE_AP && type != NL80211_IFTYPE_STATION) {
        return -EINVAL; // Invalid interface type
    }

    // Update the interface type
    dev->ieee80211_ptr->iftype = type;

    // Perform any additional configuration needed for the new type
    if (type == NL80211_IFTYPE_AP) {
        // Initialize AP-specific settings
        // e.g., start beaconing, set up security parameters, etc.
    } else if (type == NL80211_IFTYPE_STATION) {
        // Initialize Station-specific settings
        // e.g., stop beaconing, clear associated clients, etc.
    }

    return 0; // Success
}

static void nvf_del_virtual_intf(struct wiphy *wiphy, struct net_device *dev) {
    struct wifi_drv_context *wifi_drv = dev_priv(dev);

    // Perform any necessary cleanup for the virtual interface
    // e.g., stop beaconing, disconnect clients, etc.

    // Unregister the device
    unregister_netdev(dev);
    free_netdev(dev); // Free the allocated net_device structure

    // Remove the device from the driver context if you maintain a list
}


/* callback that called by the kernel when user decided to scan.
 * This callback should initiate scan routine(through work_struct) and exit with 0 if everything ok.
 * Scan routine should be finished with cfg80211_scan_done() call. */
static int nvf_scan(struct wifi *wifi, struct cfg80211_scan_request *request) {
    struct wifi_drv_context *wifi_drv = wifi_get_wifi_drv_context(wifi)->wifi_drv;

    if(down_interruptible(&wifi_drv->sem)) {
        return -ERESTARTSYS;
    }

    if (wifi_drv->scan_request != NULL) {
        up(&wifi_drv->sem);
        return -EBUSY;
    }
    wifi_drv->scan_request = request;

    up(&wifi_drv->sem);

    if (!schedule_work(&wifi_drv->ws_scan)) {
        return -EBUSY;
    }

    return 0; /* OK */
}

/* callback that called by the kernel when there is need to "connect" to some network.
 * It inits connection routine through work_struct and exits with 0 if everything ok.
 * connect routine should be finished with cfg80211_connect_bss()/cfg80211_connect_result()/cfg80211_connect_done() or cfg80211_connect_timeout(). */
static int nvf_connect(struct wifi *wifi, struct net_device *dev,
                struct cfg80211_connect_params *sme) {
    struct wifi_drv_context *wifi_drv = wifi_get_wifi_drv_context(wifi)->wifi_drv;
    size_t ssid_len = sme->ssid_len > 15 ? 15 : sme->ssid_len;

    if(down_interruptible(&wifi_drv->sem)) {
        return -ERESTARTSYS;
    }

    memcpy(wifi_drv->connecting_ssid, sme->ssid, ssid_len);
    wifi_drv->connecting_ssid[ssid_len] = 0;

    up(&wifi_drv->sem);

    if (!schedule_work(&wifi_drv->ws_connect)) {
        return -EBUSY;
    }
    return 0;
}
/* callback that called by the kernel when there is need to "diconnect" from currently connected network.
 * It inits disconnect routine through work_struct and exits with 0 if everything ok.
 * disconnect routine should call cfg80211_disconnected() to inform the kernel that disconnection is complete. */
static int nvf_disconnect(struct wifi *wifi, struct net_device *dev,
                   u16 reason_code) {
    struct wifi_drv_context *wifi_drv = wifi_get_wifi_drv_context(wifi)->wifi_drv;

    if(down_interruptible(&wifi_drv->sem)) {
        return -ERESTARTSYS;
    }

    wifi_drv->disconnect_reason_code = reason_code;

    up(&wifi_drv->sem);

    if (!schedule_work(&wifi_drv->ws_disconnect)) {
        return -EBUSY;
    }
    return 0;
}

/* Structure of functions for FullMAC 80211 drivers.
 * Functions that implemented along with fields/flags in wifi structure would represent drivers features.
 * This DEMO can only perform "scan" and "connect".
 * Some functions cant be implemented alone, for example: with "connect" there is should be function "disconnect". */
static struct cfg80211_ops nvf_cfg_ops = {
        .scan = nvf_scan,
        .connect = nvf_connect,
        .disconnect = nvf_disconnect,
        .add_virtual_intf = nvf_add_virtual_intf, // Add callbacks for AP mode
        .change_virtual_intf = nvf_change_virtual_intf, // Add callbacks for AP mode
        .del_virtual_intf = nvf_del_virtual_intf, // Add callbacks for AP mode
};

/* Network packet transmit.
 * Callback that called by the kernel when packet of data should be sent.
 * In this example it does nothing. */
static netdev_tx_t nvf_ndo_start_xmit(struct sk_buff *skb,
                               struct net_device *dev) {
    /* Dont forget to cleanup skb, as its ownership moved to xmit callback. */
    kfree_skb(skb);
    return NETDEV_TX_OK;
}

/* Structure of functions for network devices.
 * It should have at least ndo_start_xmit functions that called for packet to be sent. */
static struct net_device_ops nvf_ndev_ops = {
        .ndo_start_xmit = nvf_ndo_start_xmit,
};

/* Array of "supported" channels in 2ghz band. It's required for wifi.
 * For demo - the only channel 6. */
static struct ieee80211_channel nvf_supported_channels_2ghz[] = {
    {
        .band = NL80211_BAND_2GHZ,
        .hw_value = 6,
        .center_freq = 2437,
        .flags = IEEE80211_CHAN_NO_IBSS, 
    },
    {
        .band = NL80211_BAND_2GHZ,
        .hw_value = 1,
        .center_freq = 2412,
        .flags = IEEE80211_CHAN_NO_IBSS,
    },
    {
        .band = NL80211_BAND_2GHZ,
        .hw_value = 11,
        .center_freq = 2462,
        .flags = IEEE80211_CHAN_NO_IBSS,
    },
    // Add additional channels as needed for 40 MHz support
};

/* Array of supported rates. Its required to support at least those next rates for 2ghz band. */
static struct ieee80211_rate nvf_supported_rates_2ghz[] = {
    {
        .bitrate = 10,
        .hw_value = 0x1,
    },
    {
        .bitrate = 20,
        .hw_value = 0x2,
    },
    {
        .bitrate = 55,
        .hw_value = 0x4,
    },
    {
        .bitrate = 110,
        .hw_value = 0x8,
    },
    {
        .bitrate = 30, // 40 MHz rate
        .hw_value = 0x10,
    },
    {
        .bitrate = 60, // 40 MHz rate
        .hw_value = 0x20,
    },
    {
        .bitrate = 150, // 40 MHz rate
        .hw_value = 0x40,
    },
    {
        .bitrate = 300, // 40 MHz rate
        .hw_value = 0x80,
    },
};

/* Structure that describes supported band of 2ghz. */
static struct ieee80211_supported_band nf_band_2ghz = {
    .ht_cap.cap = IEEE80211_HT_CAP_SGI_20 | IEEE80211_HT_CAP_SGI_40, // Enable short guard interval for both 20 and 40 MHz
    .ht_cap.ht_supported = true, // Indicate that HT is supported
    .channels = nvf_supported_channels_2ghz,
    .n_channels = ARRAY_SIZE(nvf_supported_channels_2ghz),
    .bitrates = nvf_supported_rates_2ghz,
    .n_bitrates = ARRAY_SIZE(nvf_supported_rates_2ghz),
};

/* Function that creates wifi context and net_device with wireless_dev.
 * wifi/net_device/wireless_dev is basic interfaces for the kernel to interact with driver as wireless one.
 * It returns driver's main "wifi_drv" context. */
static struct wifi_drv_context *wifi_drv_create_context(void) {
    struct wifi_drv_context *ret = NULL;
    struct wifi_drv_wifi_priv_context *wifi_data = NULL;
    struct wifi_drv_ndev_priv_context *ndev_data = NULL;

    /* allocate for wifi_drv context*/
    ret = kmalloc(sizeof(*ret), GFP_KERNEL);
    if (!ret) {
        goto l_error;
    }

    /* allocate wifi context, also it possible just to use wifi_new() function.
     * wifi should represent physical FullMAC wireless device.
     * One wifi can have serveral network interfaces - for that u need to implement add_virtual_intf() and co. from cfg80211_ops. */
    ret->wifi = wifi_new_nm(&nvf_cfg_ops, sizeof(struct wifi_drv_wifi_priv_context), WIFI_NAME);
    if (ret->wifi == NULL) {
        goto l_error_wifi;
    }

    /* save wifi_drv context in wifi private data. */
    wifi_data = wifi_get_wifi_drv_context(ret->wifi);
    wifi_data->wifi_drv = ret;

    /* set device object as wifi "parent", I dont have any device yet. */
    /* set_wifi_dev(ret->wifi, dev); */

    /* wifi should determinate it type */
    /* add other required types like  "BIT(NL80211_IFTYPE_STATION) | BIT(NL80211_IFTYPE_AP)" etc. */
    ret->wifi->interface_modes = BIT(NL80211_IFTYPE_STATION) | BIT(NL80211_IFTYPE_AP);

    /* wifi should have at least 1 band. */
    /* fill also NL80211_BAND_5GHZ if required, in this small example I use only 1 band with 1 "channel" */
    ret->wifi->bands[NL80211_BAND_2GHZ] = &nf_band_2ghz;

    /* scan - if ur device supports "scan" u need to define max_scan_ssids at least. */
    ret->wifi->max_scan_ssids = 69;

    /* register wifi, if everything ok - there should be another wireless device in system.
     * use command:
     *     $ iw list
     *     Wiphy wifi_drv
     *     ...
     * */
    if (wifi_register(ret->wifi) < 0) {
        goto l_error_wifi_register;
    }

    /* allocate network device context. */
    ret->ndev = alloc_netdev(sizeof(*ndev_data), NDEV_NAME, NET_NAME_ENUM, ether_setup);
    if (ret->ndev == NULL) {
        goto l_error_alloc_ndev;
    }
    /* fill private data of network context.*/
    ndev_data = ndev_get_wifi_drv_context(ret->ndev);
    ndev_data->wifi_drv = ret;

    /* fill wireless_dev context.
     * wireless_dev with net_device can be represented as inherited class of single net_device. */
    ndev_data->wdev.wifi = ret->wifi;
    ndev_data->wdev.netdev = ret->ndev;
    ndev_data->wdev.iftype = NL80211_IFTYPE_STATION;
    ret->ndev->ieee80211_ptr = &ndev_data->wdev;

    /* set device object for net_device */
    /* SET_NETDEV_DEV(ret->ndev, wifi_dev(ret->wifi)); */

    /* set network device hooks. It should implement ndo_start_xmit() at least. */
    ret->ndev->netdev_ops = &nvf_ndev_ops;

    /* Add here proper net_device initialization. */

    /* register network device. If everything ok, there should be new network device:
     *     $ ip a
     *     ...
     *     4: wifi_drv0: <BROADCAST,MULTICAST> mtu 1500 qdisc noop state DOWN group default qlen 1000
     *         link/ether 00:00:00:00:00:00 brd ff:ff:ff:ff:ff:ff
     *     ...
     * */
    if (register_netdev(ret->ndev)) {
        goto l_error_ndev_register;
    }

    return ret;
    l_error_ndev_register:
    free_netdev(ret->ndev);
    l_error_alloc_ndev:
    wifi_unregister(ret->wifi);
    l_error_wifi_register:
    wifi_free(ret->wifi);
    l_error_wifi:
    kfree(ret);
    l_error:
    return NULL;
}

static void wifi_drv_free(struct wifi_drv_context *ctx) {
    if (ctx == NULL) {
        return;
    }

    unregister_netdev(ctx->ndev);
    free_netdev(ctx->ndev);
    wifi_unregister(ctx->wifi);
    wifi_free(ctx->wifi);
    kfree(ctx);
}

static struct wifi_drv_context *g_ctx = NULL;

static int __init virtual_wifi_init(void) {
    g_ctx = wifi_drv_create_context();

    if (g_ctx != NULL) {
        /*DEMO*/
        sema_init(&g_ctx->sem, 1);
        INIT_WORK(&g_ctx->ws_connect, wifi_drv_connect_routine);
        g_ctx->connecting_ssid[0] = 0;
        INIT_WORK(&g_ctx->ws_disconnect, wifi_drv_disconnect_routine);
        g_ctx->disconnect_reason_code = 0;
        INIT_WORK(&g_ctx->ws_scan, wifi_drv_scan_routine);
        g_ctx->scan_request = NULL;
    }
    return g_ctx == NULL;
}

static void __exit virtual_wifi_exit(void) {
    /* make sure that no work is queued */
    cancel_work_sync(&g_ctx->ws_connect);
    cancel_work_sync(&g_ctx->ws_disconnect);
    cancel_work_sync(&g_ctx->ws_scan);

    wifi_drv_free(g_ctx);
}

module_init(virtual_wifi_init);
module_exit(virtual_wifi_exit);

MODULE_LICENSE("GPL v2");

MODULE_DESCRIPTION("Example for cfg80211(aka FullMAC) driver."
                   "Module creates wireless device with network."
                   "The device can work both as station(STA mode) and HostAP modes."
                   "The device can perform scan that \"scans\" only network."
                   "Also it performs \"connect\" to the network.");