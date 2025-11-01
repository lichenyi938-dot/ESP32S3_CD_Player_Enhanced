/**
 * 枚举设备并实现 USB Host 端的基础传输
 * Implement basic commands for USB hosts and identify MSC devices
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"

#include "esp_log.h"
#include "esp_err.h"
#include "esp_check.h"

#include "usbhost_driver.h"  // 项目里的头文件，定义 usbhost_driver_t 等

// 如果你的 SCSI 层会用到互斥，这里保持外部声明（项目里已有）
extern SemaphoreHandle_t scsiExeLock;

/* ---------------- 内部消息 ---------------- */

#define CLASS_DRIVER_ACTION_NEW_DEV   0x01
#define CLASS_DRIVER_ACTION_CLOSE_DEV 0x02

#define DAEMON_TASK_PRIORITY 2
#define CLIENT_TASK_PRIORITY 3

/* ---------------- 全局对象 ---------------- */

QueueHandle_t queue_client = NULL;
usbhost_driver_t usbhost_driverObj = {0};

static const char *TAG = "usbhost";

/* ---------------- 工具函数 ---------------- */

static inline void send_msg_to_client(uint8_t msg)
{
    if (queue_client) xQueueSend(queue_client, &msg, 0);
}

/* ---------------- 打开/关闭设备 ---------------- */

esp_err_t usbhost_openDevice(void)
{
    ESP_LOGI(TAG, "Open device, addr=%u", (unsigned)usbhost_driverObj.dev_addr);

    // 打开设备
    ESP_RETURN_ON_ERROR(
        usb_host_device_open(usbhost_driverObj.handle_client,
                             usbhost_driverObj.dev_addr,
                             &usbhost_driverObj.handle_device),
        TAG, "usb_host_device_open failed");

    // 读取设备信息
    usb_device_info_t dev_info = {0};
    ESP_RETURN_ON_ERROR(usb_host_device_info(usbhost_driverObj.handle_device, &dev_info),
                        TAG, "usb_host_device_info failed");

    printf("USB speed: %s speed\n", (dev_info.speed == USB_SPEED_LOW) ? "Low" : "Full");
    printf("bConfigurationValue: %u\n", (unsigned)dev_info.bConfigurationValue);
    printf("string desc manufacturer: ");  usb_print_string_descriptor(dev_info.str_desc_manufacturer);
    printf("string desc product:      ");  usb_print_string_descriptor(dev_info.str_desc_product);
    printf("string desc sn:           ");  usb_print_string_descriptor(dev_info.str_desc_serial_num);

    // 读取设备描述符
    const usb_device_desc_t *dev_desc = NULL;
    ESP_RETURN_ON_ERROR(usb_host_get_device_descriptor(usbhost_driverObj.handle_device, &dev_desc),
                        TAG, "get_device_descriptor failed");
    usb_print_device_descriptor(dev_desc);

    // 读取配置/接口/端点描述符
    const usb_config_desc_t *config_desc = NULL;
    ESP_RETURN_ON_ERROR(usb_host_get_active_config_descriptor(usbhost_driverObj.handle_device, &config_desc),
                        TAG, "get_active_config_descriptor failed");
    usb_print_config_descriptor(config_desc, NULL);

    int offset = 0;
    const usb_standard_desc_t *each_desc = (const usb_standard_desc_t *)config_desc;
    while (each_desc != NULL) {
        if (each_desc->bDescriptorType == USB_B_DESCRIPTOR_TYPE_INTERFACE) {
            usbhost_driverObj.desc_interface = (usb_intf_desc_t *)each_desc;
        } else if (each_desc->bDescriptorType == USB_B_DESCRIPTOR_TYPE_ENDPOINT) {
            usb_ep_desc_t *ep = (usb_ep_desc_t *)each_desc;
            uint8_t epAddr = ep->bEndpointAddress;
            uint8_t type   = ep->bmAttributes & 0x3;

            if ((epAddr & 0x80) && type == 2) {          // IN  BULK
                usbhost_driverObj.desc_ep_in = ep;
            } else if ((!(epAddr & 0x80)) && type == 2) { // OUT BULK
                usbhost_driverObj.desc_ep_out = ep;
            }
        }

        if (usbhost_driverObj.desc_interface &&
            usbhost_driverObj.desc_ep_in &&
            usbhost_driverObj.desc_ep_out) {
            break;
        }
        each_desc = usb_parse_next_descriptor(each_desc, config_desc->wTotalLength, &offset);
    }

    // 判断是否为 MSC Bulk-Only
    if (usbhost_driverObj.desc_interface == NULL ||
        usbhost_driverObj.desc_ep_in == NULL ||
        usbhost_driverObj.desc_ep_out == NULL) {
        printf("Endpoint/interface descriptor not found.\n");
        return ESP_FAIL;
    }

    if (usbhost_driverObj.desc_interface->bInterfaceClass    == 0x08 &&   // Mass Storage
        (usbhost_driverObj.desc_interface->bInterfaceSubClass == 0x05 ||  // Obsolete SFF-8070i
         usbhost_driverObj.desc_interface->bInterfaceSubClass == 0x06 ||  // SCSI transparent
         usbhost_driverObj.desc_interface->bInterfaceSubClass == 0x02) && // MMC-5
        usbhost_driverObj.desc_interface->bInterfaceProtocol == 0x50) {   // Bulk-Only
        printf("USB Mass Storage Class Bulk-Only device\n");
    } else {
        printf("Not BBB device\n");
        return ESP_FAIL;
    }

    // 记录端点号和包长
    usbhost_driverObj.ep_in_num       = usbhost_driverObj.desc_ep_in->bEndpointAddress;   // 不要 &0x0F
    usbhost_driverObj.ep_in_packsize  = usbhost_driverObj.desc_ep_in->wMaxPacketSize;
    usbhost_driverObj.ep_out_num      = usbhost_driverObj.desc_ep_out->bEndpointAddress;  // 不要 &0x0F
    usbhost_driverObj.ep_out_packsize = usbhost_driverObj.desc_ep_out->wMaxPacketSize;

    printf("ep in : %u, packsize: %u\n",  (unsigned)usbhost_driverObj.ep_in_num,  (unsigned)usbhost_driverObj.ep_in_packsize);
    printf("ep out: %u, packsize: %u\n",  (unsigned)usbhost_driverObj.ep_out_num, (unsigned)usbhost_driverObj.ep_out_packsize);

    // 申请传输对象
    ESP_RETURN_ON_ERROR(usb_host_transfer_alloc(usbhost_driverObj.ep_out_packsize, 0, &usbhost_driverObj.transferObj),
                        TAG, "usb_host_transfer_alloc fail");

    // 声明接口
    ESP_RETURN_ON_ERROR(usb_host_interface_claim(usbhost_driverObj.handle_client,
                                                 usbhost_driverObj.handle_device,
                                                 usbhost_driverObj.desc_interface->bInterfaceNumber,
                                                 usbhost_driverObj.desc_interface->bAlternateSetting),
                        TAG, "usb_host_interface_claim fail");

    // 给设备一点时间初始化
    vTaskDelay(pdMS_TO_TICKS(1234));
    usbhost_driverObj.deviceIsOpened = 1;
    return ESP_OK;
}

void usbhost_closeDevice(void)
{
    if (usbhost_driverObj.handle_device == NULL) return;

    usb_host_interface_release(usbhost_driverObj.handle_client,
                               usbhost_driverObj.handle_device,
                               usbhost_driverObj.desc_interface->bInterfaceNumber);

    usb_host_device_close(usbhost_driverObj.handle_client, usbhost_driverObj.handle_device);

    if (usbhost_driverObj.transferObj) {
        usb_host_transfer_free(usbhost_driverObj.transferObj);
        usbhost_driverObj.transferObj = NULL;
    }

    usbhost_driverObj.handle_device    = NULL;
    usbhost_driverObj.desc_interface   = NULL;
    usbhost_driverObj.desc_ep_out      = NULL;
    usbhost_driverObj.desc_ep_in       = NULL;
    usbhost_driverObj.dev_addr         = 0;
    usbhost_driverObj.ep_in_num        = 0;
    usbhost_driverObj.ep_in_packsize   = 0;
    usbhost_driverObj.ep_out_num       = 0;
    usbhost_driverObj.ep_out_packsize  = 0;
    usbhost_driverObj.deviceIsOpened   = 0;
}

/* ---------------- Client 回调 & 任务 ---------------- */

static void usbhost_cb_client(const usb_host_client_event_msg_t *event_msg, void *arg)
{
    usbhost_driver_t *ctx = (usbhost_driver_t *)arg;
    switch (event_msg->event) {
    case USB_HOST_CLIENT_EVENT_NEW_DEV:
        if (event_msg->new_dev.address != 0 && ctx->handle_device == NULL) {
            ctx->dev_addr = event_msg->new_dev.address;
            send_msg_to_client(CLASS_DRIVER_ACTION_NEW_DEV);
        }
        break;
    case USB_HOST_CLIENT_EVENT_DEV_GONE:
        if (ctx->handle_device != NULL) {
            send_msg_to_client(CLASS_DRIVER_ACTION_CLOSE_DEV);
        }
        break;
    default:
        // Should never occur
        abort();
    }
}

static void usbhost_task_client(void *arg)
{
    queue_client = xQueueCreate(10, sizeof(uint8_t));

    ESP_LOGI(TAG, "Registering Client");
    usb_host_client_config_t client_config = {
        .is_synchronous = false,
        .max_num_event_msg = 5,
        .async = {
            .client_event_callback = usbhost_cb_client,
            .callback_arg = (void *)&usbhost_driverObj,
        },
    };
    ESP_ERROR_CHECK(usb_host_client_register(&client_config, &usbhost_driverObj.handle_client));

    uint8_t msg = 0;
    while (1) {
        // 阻塞等待 USB Host 事件
        usb_host_client_handle_events(usbhost_driverObj.handle_client, portMAX_DELAY);

        BaseType_t ok = xQueueReceive(queue_client, &msg, 0);
        if (ok == pdTRUE) {
            switch (msg) {
            case CLASS_DRIVER_ACTION_NEW_DEV: {
                printf("USB device connected.\n");
                esp_err_t ret = usbhost_openDevice();
                if (ret != ESP_OK) {
                    usbhost_closeDevice();
                }
                break;
            }
            case CLASS_DRIVER_ACTION_CLOSE_DEV:
                printf("USB device disconnected.\n");
                usbhost_closeDevice();
                break;
            default:
                break;
            }
        }
    }
    // 不会走到这里
}

static void usbhost_task_usblibDaemon(void *arg)
{
    while (1) {
        uint32_t event_flags = 0;
        ESP_ERROR_CHECK(usb_host_lib_handle_events(portMAX_DELAY, &event_flags));
        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
            ESP_LOGI(TAG, "USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS");
        }
        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE) {
            ESP_LOGI(TAG, "USB_HOST_LIB_EVENT_FLAGS_ALL_FREE");
        }
    }
}

/* ---------------- 对外初始化 ---------------- */

void usbhost_driverInit(void)
{
    ESP_LOGI(TAG, "Installing USB Host Library");
    usb_host_config_t host_config = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
        .enum_filter_cb = NULL,
    };
    ESP_ERROR_CHECK(usb_host_install(&host_config));

    BaseType_t ret;

    // USB Host 守护任务
    ret = xTaskCreatePinnedToCore(usbhost_task_usblibDaemon,
                                  "usbhost_task_usblibDaemon",
                                  4096, NULL, DAEMON_TASK_PRIORITY, NULL, 1);
    if (ret != pdPASS) ESP_LOGE(TAG, "usbhost_task_usblibDaemon create fail");

    // Client 任务
    ret = xTaskCreatePinnedToCore(usbhost_task_client,
                                  "usbhost_task_client",
                                  4096, NULL, CLIENT_TASK_PRIORITY, NULL, 1);
    if (ret != pdPASS) ESP_LOGE(TAG, "usbhost_task_client create fail");

    vTaskDelay(pdMS_TO_TICKS(10)); // 给 client 任务一点时间

    usbhost_driverObj.transferDone = xSemaphoreCreateBinary();
    scsiExeLock = xSemaphoreCreateMutex();
}

/* ---------------- 传输层 ---------------- */

static void usbhost_cb_transfer(usb_transfer_t *transfer)
{
    if (transfer->status != USB_TRANSFER_STATUS_COMPLETED) {
        ESP_LOGE(TAG, "Transfer failed Status %d", transfer->status);
    }
    xSemaphoreGive(usbhost_driverObj.transferDone);
}

static usb_transfer_status_t usbhost_waitForTransDone(usb_transfer_t *xfer)
{
    BaseType_t ret = xSemaphoreTake(usbhost_driverObj.transferDone,
                                    pdMS_TO_TICKS(xfer->timeout_ms));
    usb_transfer_status_t status = xfer->status;

    if (ret != pdTRUE) {
        ESP_LOGE(TAG, "time out, stop/clear endpoint 0x%02X", xfer->bEndpointAddress);
        usb_host_endpoint_halt (xfer->device_handle, xfer->bEndpointAddress);
        usb_host_endpoint_flush(xfer->device_handle, xfer->bEndpointAddress);
        usb_host_endpoint_clear(xfer->device_handle, xfer->bEndpointAddress);
        // flush 后应立即返回
        xSemaphoreTake(usbhost_driverObj.transferDone, portMAX_DELAY);
        status = USB_TRANSFER_STATUS_TIMED_OUT;
    }
    return status;
}

esp_err_t usbhost_clearFeature(uint8_t endpoint)
{
    esp_err_t ret = usb_host_endpoint_halt(usbhost_driverObj.handle_device, endpoint);
    if (ret != ESP_OK) return ret;

    ret = usb_host_endpoint_flush(usbhost_driverObj.handle_device, endpoint);
    if (ret != ESP_OK) {
        // 没有 STALL 状态时 flush 会失败，这里不打印 error
        return ret;
    }

    ret = usb_host_endpoint_clear(usbhost_driverObj.handle_device, endpoint);
    if (ret != ESP_OK) return ret;

    usb_setup_packet_t setupPack = {
        .bmRequestType = 0x02, // to endpoint
        .bRequest      = 1,    // CLEAR_FEATURE
        .wValue        = 0,
        .wIndex        = endpoint,
        .wLength       = 0,
    };
    return usbhost_controlTransfer(&setupPack, 8);
}

esp_err_t usbhost_controlTransfer(void *data, size_t size)
{
    usb_transfer_t *xfer = usbhost_driverObj.transferObj;

    memcpy(xfer->data_buffer, data, size);
    xfer->bEndpointAddress = 0; // EP0
    xfer->num_bytes        = size;
    xfer->callback         = usbhost_cb_transfer;
    xfer->context          = NULL;
    xfer->timeout_ms       = 5000;
    xfer->device_handle    = usbhost_driverObj.handle_device;

    ESP_RETURN_ON_ERROR(usb_host_transfer_submit_control(usbhost_driverObj.handle_client, xfer),
                        TAG, "usb_host_transfer_submit_control failed");

    usb_transfer_status_t status = usbhost_waitForTransDone(xfer);
    if (status != USB_TRANSFER_STATUS_COMPLETED) {
        ESP_LOGE(TAG, "Control transfer fail: %d", status);
        return ESP_FAIL;
    }

    memcpy(data, xfer->data_buffer, size);
    return ESP_OK;
}

esp_err_t usbhost_bulkTransfer(void *data, uint32_t *size, usbhost_transDir_t dir, uint32_t timeoutMs)
{
    usb_transfer_t *xfer = usbhost_driverObj.transferObj;

    // IN 方向要对齐到 MPS，否则 IDF 会按提交长度取数
    size_t need = (dir == DEV_TO_HOST)
                    ? usb_round_up_to_mps(*size, usbhost_driverObj.ep_in_packsize)
                    : (size_t)*size;

    if (xfer->data_buffer_size < need) {
        usb_host_transfer_free(xfer);
        usb_host_transfer_alloc(need, 0, &usbhost_driverObj.transferObj);
        xfer = usbhost_driverObj.transferObj;
    }

    if (dir == HOST_TO_DEV) {
        memcpy(xfer->data_buffer, data, *size);
        xfer->bEndpointAddress = usbhost_driverObj.ep_out_num;
    } else {
        xfer->bEndpointAddress = usbhost_driverObj.ep_in_num;  // IN 端点 bit7 必须为 1
    }

    xfer->num_bytes     = need;
    xfer->device_handle = usbhost_driverObj.handle_device;
    xfer->callback      = usbhost_cb_transfer;
    xfer->timeout_ms    = timeoutMs;
    xfer->context       = NULL;

    ESP_RETURN_ON_ERROR(usb_host_transfer_submit(xfer), TAG, "usb_host_transfer_submit failed");

    usb_transfer_status_t status = usbhost_waitForTransDone(xfer);
    *size = xfer->actual_num_bytes;

    if (status != USB_TRANSFER_STATUS_COMPLETED) {
        ESP_LOGE(TAG, "Bulk transfer fail: %d", status);
        return (esp_err_t)status;
    }

    if (dir == DEV_TO_HOST) {
        memcpy(data, xfer->data_buffer, xfer->actual_num_bytes);
    }
    return ESP_OK;
}
