# Virtual WiFi Module

This is a sample WiFi kernel module for a FullMAC WiFi Driver making use of the `cfg80211` kernel subsystem. This driver is designed to simulate a Wi-Fi device that can perform basic operations such as scanning for networks and connecting to a dummy network with a predefined SSID.

### Description:
- Supports both Station (STA) and HostAP Modes.

### Module Structure:

1. **Context Structures**:
   - `wifi_drv_context`: Holds the main context for the driver, including the `wifi` (wireless physical device) and `net_device` (network interface).
   - `wifi_drv_wifi_priv_context` and `wifi_drv_wifi_priv_context`: Private data structures for the `wifi` and `net_device`, respectively.

2. **Work Queues**:
   - The driver uses work queues to handle asynchronous operations such as connecting, disconnecting, and scanning. This is done using `work_struct` and the `schedule_work()` function.

3. **Scan and Connect Routines**:
   - `wifi_drv_scan_routine`: Simulates a scan by informing the kernel about a dummy BSS (Basic Service Set) and then calling `cfg80211_scan_done()`.
   - `wifi_drv_connect_routine`: Simulates connecting to a network by checking the SSID and calling `cfg80211_connect_bss()` or `cfg80211_connect_timeout()`.

4. **Callbacks**:
   - The driver implements several callbacks defined in the `cfg80211_ops` structure, such as `nvf_scan`, `nvf_connect`, `nvf_disconnect`, `nvf_add_virtual_intf`, `nvf_change_virtual_intf` and `nvf_del_virtual_intf` which are invoked by the kernel when the user space requests these operations.
   - The functions `wifi_drv_start_ap` and `wifi_drv_stop_ap`, are related to managing the Access Point (AP) mode of the Wi-Fi driver. Both of these functions are called through a request from a user-space utility
     such as `iw` or `nmcli` eg: `iw dev wlan0 set type ap`

5. **WiFi and Net Device Creation**:
   - The `wifi_drv_create_context()` function initializes the `wifi` and `net_device`, sets their properties, and registers them with the kernel.

6. **Module Initialization and Cleanup**:
   - The `virtual_wifi_init()` function initializes the driver and creates the context, while `virtual_wifi_exit()` cleans up and unregisters the driver.
