/****************************************************************************************************************************
  usb_host_hid_bridge.cpp
  For ESP32 S series boards

  ESP32 USB Host HID Brigde is a library for the ESP32/Arduino platform
  Built by Jeff Leung https://github.com/badjeff/ESP32-USB-Host-HID-Bridge
  Licensed under MIT license
  
  Version: 1.0.0

  Version Modified By   Date      Comments
  ------- -----------  ---------- -----------
  1.0.0   Jeff Leung   06/07/2022 Initial coding
  1.0.1   Jeff Leung   06/12/2022 Deprecapte semaphores
 *****************************************************************************************************************************/

#include <Arduino.h>
#include "usb_host_hid_bridge.h"

// bit mask for async tasks
#define ACTION_OPEN_DEV             0x01
#define ACTION_GET_DEV_INFO         0x02
#define ACTION_GET_DEV_DESC         0x04
#define ACTION_GET_CONFIG_DESC      0x08
#define ACTION_GET_STR_DESC         0x10
#define ACTION_CLOSE_DEV            0x20
#define ACTION_EXIT                 0x40
#define ACTION_CLAIM_INTF                       0x0100
#define ACTION_TRANSFER_CTRL_GET_REPORT_DESC    0x0200
#define ACTION_TRANSFER_INTR_GET_REPORT         0x0400

typedef struct {
    usb_host_client_handle_t client_hdl;
    uint8_t dev_addr;
    usb_device_handle_t dev_hdl;
    uint32_t actions;
    uint16_t bMaxPacketSize0;
    usb_ep_desc_t *ep_in;
    usb_ep_desc_t *ep_out;
    UsbHostHidBridge *bdg;
} class_driver_t;

TaskHandle_t _daemon_task_hdl;
TaskHandle_t _class_driver_task_hdl;

static const char *TAG_DAEMON = "DAEMON";
static const char *TAG_CLASS = "CLASS";

static void client_event_cb(const usb_host_client_event_msg_t *event_msg, void *arg)
{
    class_driver_t *driver_obj = (class_driver_t *)arg;
    switch (event_msg->event) {
        case USB_HOST_CLIENT_EVENT_NEW_DEV:
            if (driver_obj->dev_addr == 0) {
                driver_obj->dev_addr = event_msg->new_dev.address;
                //Open the device next
                driver_obj->actions |= ACTION_OPEN_DEV;
            }
            break;
        case USB_HOST_CLIENT_EVENT_DEV_GONE:
            if (driver_obj->dev_hdl != NULL) {
                //Cancel any other actions and close the device next
                driver_obj->actions = ACTION_CLOSE_DEV;
            }
            break;
        default:
            //Should never occur
            abort();
    }
}

static void action_open_dev(class_driver_t *driver_obj)
{
    assert(driver_obj->dev_addr != 0);
    ESP_LOGI(TAG_CLASS, "Opening device at address %d", driver_obj->dev_addr);
    ESP_ERROR_CHECK(usb_host_device_open(driver_obj->client_hdl, driver_obj->dev_addr, &driver_obj->dev_hdl));
    
    //Get the device's information next
    driver_obj->actions &= ~ACTION_OPEN_DEV;
    driver_obj->actions |= ACTION_GET_DEV_INFO;
}

static void action_get_info(class_driver_t *driver_obj)
{
    assert(driver_obj->dev_hdl != NULL);
    ESP_LOGI(TAG_CLASS, "Getting device information");
    usb_device_info_t dev_info;
    ESP_ERROR_CHECK(usb_host_device_info(driver_obj->dev_hdl, &dev_info));

    // ESP_LOGI(TAG_CLASS, "\t%s speed", (dev_info.speed == USB_SPEED_LOW) ? "Low" : "Full");
    // ESP_LOGI(TAG_CLASS, "\tbConfigurationValue %d", dev_info.bConfigurationValue);

    //Get the device descriptor next
    driver_obj->actions &= ~ACTION_GET_DEV_INFO;
    driver_obj->actions |= ACTION_GET_DEV_DESC;
}

static void action_get_dev_desc(class_driver_t *driver_obj)
{
    assert(driver_obj->dev_hdl != NULL);
    ESP_LOGI(TAG_CLASS, "Getting device descriptor");
    const usb_device_desc_t *dev_desc;
    ESP_ERROR_CHECK(usb_host_get_device_descriptor(driver_obj->dev_hdl, &dev_desc));

    driver_obj->bMaxPacketSize0 = dev_desc->bMaxPacketSize0; // shall be used in action_transfer_control()
    // ESP_LOGI(TAG_CLASS, "\tidVendor 0x%04x", dev_desc->idVendor);
    // ESP_LOGI(TAG_CLASS, "\tidProduct 0x%04x", dev_desc->idProduct);
    // usb_print_device_descriptor(dev_desc);

    //Get the device's config descriptor next
    driver_obj->actions &= ~ACTION_GET_DEV_DESC;
    driver_obj->actions |= ACTION_GET_CONFIG_DESC;
}

static void action_get_config_desc(class_driver_t *driver_obj)
{
    assert(driver_obj->dev_hdl != NULL);
    ESP_LOGI(TAG_CLASS, "Getting config descriptor");
    const usb_config_desc_t *config_desc;
    ESP_ERROR_CHECK(usb_host_get_active_config_descriptor(driver_obj->dev_hdl, &config_desc));
    UsbHostHidBridge *bdg = (UsbHostHidBridge *)driver_obj->bdg;
    if (bdg->onConfigDescriptorReceived != NULL) {
        bdg->onConfigDescriptorReceived(config_desc);
    }
    //Get the device's string descriptors next
    driver_obj->actions &= ~ACTION_GET_CONFIG_DESC;
    driver_obj->actions |= ACTION_GET_STR_DESC;
}

static void action_get_str_desc(class_driver_t *driver_obj)
{
    assert(driver_obj->dev_hdl != NULL);
    usb_device_info_t dev_info;
    ESP_ERROR_CHECK(usb_host_device_info(driver_obj->dev_hdl, &dev_info));
    UsbHostHidBridge *bdg = (UsbHostHidBridge *)driver_obj->bdg;
    if (bdg->onDeviceInfoReceived != NULL) {
        bdg->onDeviceInfoReceived(&dev_info);
    }
    //Claim the interface next
    driver_obj->actions &= ~ACTION_GET_STR_DESC;
    driver_obj->actions |= ACTION_CLAIM_INTF;
}

static void action_claim_interface(class_driver_t *driver_obj)
{
    assert(driver_obj->dev_hdl != NULL);
    ESP_LOGI(TAG_CLASS, "Getting config descriptor");
    const usb_config_desc_t *config_desc;
    ESP_ERROR_CHECK(usb_host_get_active_config_descriptor(driver_obj->dev_hdl, &config_desc));

    bool hidIntfClaimed = false;
    int offset = 0;
    for (size_t n = 0; n < config_desc->bNumInterfaces; n++)
    {
        const usb_intf_desc_t *intf = usb_parse_interface_descriptor(config_desc, n, 0, &offset);
        ESP_LOGI(TAG_CLASS, "Parsed intf->bInterfaceNumber: 0x%02x \n", intf->bInterfaceNumber);
        ESP_LOGI(TAG_CLASS, "Parsed intf->bInterfaceClass: 0x%02x \n", intf->bInterfaceClass);
        
        if (intf->bInterfaceClass == 0x03) // HID - https://www.usb.org/defined-class-codes
        {
            ESP_LOGI(TAG_CLASS, "Detected HID intf->bInterfaceClass: 0x%02x \n", intf->bInterfaceClass);

            const usb_ep_desc_t *ep_in = NULL;
            const usb_ep_desc_t *ep_out = NULL;
            const usb_ep_desc_t *ep = NULL;
            for (size_t i = 0; i < intf->bNumEndpoints; i++) {
                int _offset = 0;
                ep = usb_parse_endpoint_descriptor_by_index(intf, i, config_desc->wTotalLength, &_offset);
                ESP_LOGI(TAG_CLASS, "\t > Detected EP num: %d/%d, len: %d, ", i + 1, intf->bNumEndpoints, config_desc->wTotalLength);
                ESP_LOGI(TAG_CLASS, "\t   address: 0x%02x, mps: %d, dir: %s", ep->bEndpointAddress, ep->wMaxPacketSize, (ep->bEndpointAddress & 0x80) ? "IN" : "OUT");
                if (ep) {
                    if (ep->bmAttributes != USB_TRANSFER_TYPE_INTR) {
                        // only support INTERRUPT > IN Report in action_transfer() for now
                        continue;
                    }
                    if (ep->bEndpointAddress & 0x80) {
                        ep_in = ep;
                        driver_obj->ep_in = (usb_ep_desc_t *)ep_in;
                    } else {
                        ep_out = ep;
                        driver_obj->ep_out = (usb_ep_desc_t *)ep_out;
                    }
                } else {
                    ESP_LOGW("", "error to parse endpoint by index; EP num: %d/%d, len: %d", i + 1, intf->bNumEndpoints, config_desc->wTotalLength);
                }
            }
            esp_err_t err = usb_host_interface_claim(driver_obj->client_hdl, driver_obj->dev_hdl, n, 0);
            if (err) {
                ESP_LOGI("", "interface claim status: %d", err);
            } else {
                ESP_LOGI(TAG_CLASS, "Claimed HID intf->bInterfaceNumber: 0x%02x \n", intf->bInterfaceNumber);
                hidIntfClaimed = true;
            }
        }
    }

    //Get the HID's descriptors next
    driver_obj->actions &= ~ACTION_CLAIM_INTF;
    if (hidIntfClaimed)
    {
        driver_obj->actions |= ACTION_TRANSFER_CTRL_GET_REPORT_DESC;
    }
}

static void transfer_control_get_report_descriptor_cb(usb_transfer_t *transfer)
{
    //This is function is called from within usb_host_client_handle_events(). Don't block and try to keep it short
    //struct class_driver_control *class_driver_obj = (struct class_driver_control *)transfer->context;
    class_driver_t *driver_obj = (class_driver_t *)transfer->context;
    if (transfer->status != USB_TRANSFER_STATUS_COMPLETED) {
        ESP_LOGW("", "Transfer control failed - Status %d \n", transfer->status);
    }
    if (transfer->status == USB_TRANSFER_STATUS_COMPLETED) {
        UsbHostHidBridge *bdg = (UsbHostHidBridge *)driver_obj->bdg;
        if (transfer->actual_num_bytes > 0 && bdg->onHidReportDescriptorReceived != NULL) {
            bdg->onHidReportDescriptorReceived(transfer);
        }
        driver_obj->actions |= ACTION_TRANSFER_INTR_GET_REPORT;
    }
}

static void action_transfer_control_get_report_descriptor(class_driver_t *driver_obj)
{
    assert(driver_obj->dev_hdl != NULL);
    static uint16_t mps = driver_obj->bMaxPacketSize0;
    static uint16_t tps = usb_round_up_to_mps(1024, mps);
    static usb_transfer_t *transfer;
    if (!transfer) {
        usb_host_transfer_alloc(tps, 0, &transfer);
    }
    usb_setup_packet_t stp;

    // 0x81,        // bmRequestType: Dir: D2H, Type: Standard, Recipient: Interface
    // 0x06,        // bRequest (Get Descriptor)
    // 0x00,        // wValue[0:7]  Desc Index: 0
    // 0x22,        // wValue[8:15] Desc Type: (HID Report)
    // 0x00, 0x00,  // wIndex Language ID: 0x00
    // 0x40, 0x00,  // wLength = 64
    stp.bmRequestType = USB_BM_REQUEST_TYPE_DIR_IN | USB_BM_REQUEST_TYPE_TYPE_STANDARD | USB_BM_REQUEST_TYPE_RECIP_INTERFACE;
    stp.bRequest = USB_B_REQUEST_GET_DESCRIPTOR;
    stp.wValue = 0x2200;
    stp.wIndex = 0;
    stp.wLength = tps - 8;
    transfer->num_bytes = tps;

    memcpy(transfer->data_buffer, &stp, USB_SETUP_PACKET_SIZE);
    transfer->bEndpointAddress = 0x00;
    ESP_LOGI("", "transfer->bEndpointAddress: 0x%02X \n", transfer->bEndpointAddress);

    transfer->device_handle = driver_obj->dev_hdl;
    transfer->callback = transfer_control_get_report_descriptor_cb;
    transfer->context = (void *)driver_obj;
    transfer->timeout_ms = 1000;

    esp_err_t result = usb_host_transfer_submit_control(driver_obj->client_hdl, transfer);
    if (result != ESP_OK) {
        ESP_LOGW("", "attempting %s\n", esp_err_to_name(result));
    } else {
        // event queued, transfer_cb2 must be called eventually, clean actions flag
        driver_obj->actions &= ~ACTION_TRANSFER_CTRL_GET_REPORT_DESC;
    }
}

static void action_interrupt_get_report_cb(usb_transfer_t *transfer)
{
    //This is function is called from within usb_host_client_handle_events(). Don't block and try to keep it short
    //struct class_driver_control *class_driver_obj = (struct class_driver_control *)transfer->context;
    class_driver_t *driver_obj = (class_driver_t *)transfer->context;
    if (transfer->status != USB_TRANSFER_STATUS_COMPLETED) {
        ESP_LOGW("", "Transfer failed - Status %d \n", transfer->status);
    }
    if (transfer->status == USB_TRANSFER_STATUS_COMPLETED) {
        UsbHostHidBridge *bdg = (UsbHostHidBridge *)driver_obj->bdg;
        if (transfer->actual_num_bytes > 0 && bdg->onReportReceived != NULL) {
            bdg->onReportReceived(transfer);
        }
    }
    driver_obj->actions |= ACTION_TRANSFER_INTR_GET_REPORT;
}

static void action_interrupt_get_report(class_driver_t *driver_obj)
{
    assert(driver_obj->dev_hdl != NULL);
    static uint16_t mps = driver_obj->ep_in->wMaxPacketSize;
    // static uint16_t tps = usb_round_up_to_mps(mps, mps);
    static usb_transfer_t *transfer;
    if (!transfer) {
        usb_host_transfer_alloc(mps, 0, &transfer);
        memset(transfer->data_buffer, 0x00, mps);
    }
    transfer->num_bytes = mps;
    memset(transfer->data_buffer, 0x00, mps);
    transfer->bEndpointAddress = driver_obj->ep_in->bEndpointAddress;
    // ESP_LOGI("", "transfer->bEndpointAddress: 0x%02X \n", transfer->bEndpointAddress);
    transfer->device_handle = driver_obj->dev_hdl;
    transfer->callback = action_interrupt_get_report_cb;
    transfer->context = (void *)driver_obj;
    transfer->timeout_ms = 1000;

    esp_err_t result = usb_host_transfer_submit(transfer);
    if (result != ESP_OK) {
        ESP_LOGW("", "attempting %s\n", esp_err_to_name(result));
    } else {
        // event queued, transfer_cb2 must be called eventually, clean actions flag
        driver_obj->actions &= ~ACTION_TRANSFER_INTR_GET_REPORT;
    }
}

static void aciton_close_dev(class_driver_t *driver_obj)
{
    const usb_config_desc_t *config_desc;
    ESP_ERROR_CHECK(usb_host_get_active_config_descriptor(driver_obj->dev_hdl, &config_desc));
    
    int offset = 0;
    for (size_t n = 0; n < config_desc->bNumInterfaces; n++)
    {
        const usb_intf_desc_t *intf = usb_parse_interface_descriptor(config_desc, n, 0, &offset);
        ESP_LOGI(TAG_CLASS, "\nReleasing intf->bInterfaceNumber: 0x%02x \n", intf->bInterfaceNumber);
        if (intf->bInterfaceClass == 0x03) // HID - https://www.usb.org/defined-class-codes
        {
            ESP_LOGI(TAG_CLASS, "\nReleasing HID intf->bInterfaceClass: 0x%02x \n", intf->bInterfaceClass);
            const usb_ep_desc_t *ep_in = NULL;
            const usb_ep_desc_t *ep_out = NULL;
            const usb_ep_desc_t *ep = NULL;
            for (size_t i = 0; i < intf->bNumEndpoints; i++) {
                int _offset = 0;
                ep = usb_parse_endpoint_descriptor_by_index(intf, i, config_desc->wTotalLength, &_offset);
                if (ep) {
                    if (ep->bEndpointAddress & 0x80) {
                        ep_in = ep;
                    } else {
                        ep_out = ep;
                    }
                    ESP_LOGI(TAG_CLASS, "\t > Halting EP num: %d/%d, len: %d, ", i + 1, intf->bNumEndpoints, config_desc->wTotalLength);
                    ESP_LOGI(TAG_CLASS, "\t   address: 0x%02x, EP max size: %d, dir: %s\n", ep->bEndpointAddress, ep->wMaxPacketSize, (ep->bEndpointAddress & 0x80) ? "IN" : "OUT");
                    ESP_ERROR_CHECK(usb_host_endpoint_halt(driver_obj->dev_hdl, ep->bEndpointAddress));
                    ESP_ERROR_CHECK(usb_host_endpoint_flush(driver_obj->dev_hdl, ep->bEndpointAddress));
                }
            }
            ESP_ERROR_CHECK(usb_host_interface_release(driver_obj->client_hdl, driver_obj->dev_hdl, n));
        }
    }

    ESP_ERROR_CHECK(usb_host_device_close(driver_obj->client_hdl, driver_obj->dev_hdl));
    driver_obj->dev_hdl = NULL;
    driver_obj->dev_addr = 0;
    //We need to exit the event handler loop
    driver_obj->actions &= ~ACTION_CLOSE_DEV;
    driver_obj->actions &= ~ACTION_TRANSFER_INTR_GET_REPORT;
    driver_obj->actions |= ACTION_EXIT;
}

static void usb_class_driver_task(void *pvParameters)
{
    UsbHostHidBridge *bdg = (UsbHostHidBridge *)pvParameters;
    class_driver_t driver_obj = {0};

    //Wait until daemon task has installed USB Host Library
    while (!bdg->hostInstalled) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }

    ESP_LOGI(TAG_CLASS, "Registering Client");
    usb_host_client_config_t client_config = {
        .is_synchronous = false,    //Synchronous clients currently not supported. Set this to false
        .max_num_event_msg = CLIENT_NUM_EVENT_MSG,
        .async = {
            .client_event_callback = client_event_cb,
            .callback_arg = (void *)&driver_obj,
        },
    };
    ESP_ERROR_CHECK(usb_host_client_register(&client_config, &driver_obj.client_hdl));
    driver_obj.bdg = bdg;
    bdg->driver_ptr = &driver_obj;

    while (1) {
        if (driver_obj.actions == 0) {
            usb_host_client_handle_events(driver_obj.client_hdl, portMAX_DELAY);
        } else {
            if (driver_obj.actions & ACTION_OPEN_DEV) {
                action_open_dev(&driver_obj);
            }
            if (driver_obj.actions & ACTION_GET_DEV_INFO) {
                action_get_info(&driver_obj);
            }
            if (driver_obj.actions & ACTION_GET_DEV_DESC) {
                action_get_dev_desc(&driver_obj);
            }
            if (driver_obj.actions & ACTION_GET_CONFIG_DESC) {
                action_get_config_desc(&driver_obj);
            }
            if (driver_obj.actions & ACTION_GET_STR_DESC) {
                action_get_str_desc(&driver_obj);
            }
            if (driver_obj.actions & ACTION_CLAIM_INTF) {
                action_claim_interface(&driver_obj);
            }
            if (driver_obj.actions & ACTION_TRANSFER_CTRL_GET_REPORT_DESC) {
                action_transfer_control_get_report_descriptor(&driver_obj);
            }
            if (driver_obj.actions & ACTION_TRANSFER_INTR_GET_REPORT) {
                action_interrupt_get_report(&driver_obj);
            }
            if (driver_obj.actions & ACTION_CLOSE_DEV) {
                aciton_close_dev(&driver_obj);
            }
            if (driver_obj.actions & ACTION_EXIT) {
                break;
            }
        }
        vTaskDelay(CLASS_TASK_LOOP_DELAY);
    } // end main loop

    ESP_LOGI(TAG_CLASS, "Deregistering Client");
    bdg->driver_ptr = NULL;
    ESP_ERROR_CHECK(usb_host_client_deregister(driver_obj.client_hdl));

    vTaskSuspend(NULL);
}

static void usb_host_lib_daemon_task(void *pvParameters)
{
    UsbHostHidBridge *bdg = (UsbHostHidBridge *)pvParameters;

    ESP_LOGI(TAG_DAEMON, "Installing USB Host Library");
    usb_host_config_t host_config = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
    };
    ESP_ERROR_CHECK(usb_host_install(&host_config));

    //Signal to the class driver task that the host library is installed
    bdg->hostInstalled = true;
    vTaskDelay(DAEMON_TASK_LOOP_DELAY); //Short delay to let client task spin up

    bool has_clients = true;
    bool has_devices = true;
    while (has_clients || has_devices ) {
        uint32_t event_flags;
        ESP_ERROR_CHECK(usb_host_lib_handle_events(portMAX_DELAY, &event_flags));
        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
            has_clients = false;
        }
        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE) {
            has_devices = false;
        }
        vTaskDelay(DAEMON_TASK_LOOP_DELAY);
    } // end main loop
    ESP_LOGI(TAG_DAEMON, "No more clients and devices");

    //Uninstall the USB Host Library
    ESP_ERROR_CHECK(usb_host_uninstall());
    //Wait to be deleted
    vTaskSuspend(NULL);
}

UsbHostHidBridge::UsbHostHidBridge() :
    hostInstalled( false ),
    onConfigDescriptorReceived( NULL ),
    onDeviceInfoReceived( NULL ),
    onHidReportDescriptorReceived( NULL ),
    onReportReceived( NULL )
{
}

UsbHostHidBridge::~UsbHostHidBridge()
{
}

void UsbHostHidBridge::begin()
{
    //Create usb host lib daemon task
    xTaskCreatePinnedToCore(
        usb_host_lib_daemon_task,           /* Task function. */
        "usb_host_lib_daemon_task",         /* name of task. */
        4096,                               /* Stack size of task */
        (void *)this,                       /* parameter of the task */
        DAEMON_TASK_PRIORITY,               /* priority of the task */
        &_daemon_task_hdl,                  /* Task handle to keep track of created task */
        DAEMON_TASK_COREID);                /* pin task to core 0 */
    vTaskDelay(500); //Add a short delay to let the tasks run

    //Create usb class driver task
    xTaskCreatePinnedToCore(
        usb_class_driver_task,              /* Task function. */
        "usb_class_driver_task",            /* name of task. */
        4096,                               /* Stack size of task */
        (void *)this,                       /* parameter of the task */
        CLASS_TASK_PRIORITY,                /* priority of the task */
        &_class_driver_task_hdl,            /* Task handle to keep track of created task */
        CLASS_TASK_COREID);                 /* pin task to core 0 */
    vTaskDelay(500); //Add a short delay to let the tasks run
}

void UsbHostHidBridge::end()
{
    vTaskDelete(_class_driver_task_hdl);
    vTaskDelete(_daemon_task_hdl);
}
