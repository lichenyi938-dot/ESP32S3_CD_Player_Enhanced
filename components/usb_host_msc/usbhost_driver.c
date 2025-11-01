/**
 * 枚举设备和实现usb主机基础命令 + MSC(SCSI) 基本命令
 * Implement basic commands for USB hosts and identify devices,
 * with inline MSC BOT + basic SCSI commands so there's no external dependency.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_check.h"

#include "usbhost_driver.h"

extern SemaphoreHandle_t scsiExeLock;

#define CLASS_DRIVER_ACTION_NEW_DEV  0x01
#define CLASS_DRIVER_ACTION_CLOSE_DEV 0x02

#define DAEMON_TASK_PRIORITY 2
#define CLIENT_TASK_PRIORITY 3

QueueHandle_t queue_client = NULL;
usbhost_driver_t usbhost_driverObj = {0};

static void usbhost_cd_autostart(void); /* 前置声明 */

/* =============== 与 client/host 交互的公共工具 =============== */

static inline void senMsgToClientTask(uint8_t msg)
{
    xQueueSend(queue_client, &msg, 0);
}

/* 传输结束回调 */
static void usbhost_cb_transfer(usb_transfer_t *transfer)
{
    if (transfer->status != USB_TRANSFER_STATUS_COMPLETED) {
        ESP_LOGE("usbhost_cb_transfer", "Transfer failed Status %d", transfer->status);
    }
    xSemaphoreGive(usbhost_driverObj.transferDone);
}

static usb_transfer_status_t usbhost_waitForTransDone(usb_transfer_t *xfer)
{
    BaseType_t ret = xSemaphoreTake(usbhost_driverObj.transferDone, pdMS_TO_TICKS(xfer->timeout_ms));
    usb_transfer_status_t status = xfer->status;

    if (ret != pdTRUE) {
        /* 停止先前提交的传输并清空端点 */
        ESP_LOGE("usbhost_waitForTransDone", "time out, stop transfer.");
        usb_host_endpoint_halt(xfer->device_handle, xfer->bEndpointAddress);
        usb_host_endpoint_flush(xfer->device_handle, xfer->bEndpointAddress);
        usb_host_endpoint_clear(xfer->device_handle, xfer->bEndpointAddress);
        xSemaphoreTake(usbhost_driverObj.transferDone, portMAX_DELAY); /* flush 后会立即返回 */
        status = USB_TRANSFER_STATUS_TIMED_OUT;
    }

    return status;
}

esp_err_t usbhost_clearFeature(uint8_t endpoint)
{
    esp_err_t ret;
    ret = usb_host_endpoint_halt(usbhost_driverObj.handle_device, endpoint);
    if (ret != ESP_OK) return ret;

    ret = usb_host_endpoint_flush(usbhost_driverObj.handle_device, endpoint);
    if (ret != ESP_OK) return ret; /* 若没有 STALL，flush 会失败，直接返回 */

    ret = usb_host_endpoint_clear(usbhost_driverObj.handle_device, endpoint);
    if (ret != ESP_OK) return ret;

    usb_setup_packet_t setupPack = {
        .bmRequestType = 0x02, /* to endpoint */
        .bRequest      = 1,    /* clear feature */
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
    xfer->bEndpointAddress = 0; /* EP0 */
    xfer->num_bytes        = size;
    xfer->callback         = usbhost_cb_transfer;
    xfer->context          = NULL;
    xfer->timeout_ms       = 5000;
    xfer->device_handle    = usbhost_driverObj.handle_device;

    ESP_RETURN_ON_ERROR(usb_host_transfer_submit_control(usbhost_driverObj.handle_client, xfer),
                        "usb_host_transfer_submit_control", "");

    usb_transfer_status_t status = usbhost_waitForTransDone(xfer);
    if (status != USB_TRANSFER_STATUS_COMPLETED) {
        ESP_LOGE("usbhost_controlTransfer", "Transfer fail: %d", status);
        return ESP_FAIL;
    }

    memcpy(data, xfer->data_buffer, size);
    return ESP_OK;
}

esp_err_t usbhost_bulkTransfer(void *data, uint32_t *size, usbhost_transDir_t dir, uint32_t timeoutMs)
{
    usb_transfer_t *xfer = usbhost_driverObj.transferObj;

    /* 缓冲区过小重新分配：IN 方向要按 MPS 对齐 */
    size_t transfer_size = (dir == DEV_TO_HOST) ? usb_round_up_to_mps(*size, usbhost_driverObj.ep_in_packsize) : *size;
    if (xfer->data_buffer_size < transfer_size) {
        usb_host_transfer_free(xfer);
        usb_host_transfer_alloc(transfer_size, 0, &usbhost_driverObj.transferObj);
        xfer = usbhost_driverObj.transferObj;
    }

    /* 填充数据与 EP 号 */
    if (dir == HOST_TO_DEV) {
        memcpy(xfer->data_buffer, data, *size);
        xfer->bEndpointAddress = usbhost_driverObj.ep_out_num;
    } else {
        xfer->bEndpointAddress = usbhost_driverObj.ep_in_num; /* IN 地址 bit7=1，已在描述符里 */
    }

    xfer->num_bytes     = transfer_size;
    xfer->device_handle = usbhost_driverObj.handle_device;
    xfer->callback      = usbhost_cb_transfer;
    xfer->timeout_ms    = timeoutMs;
    xfer->context       = NULL;

    ESP_RETURN_ON_ERROR(usb_host_transfer_submit(xfer), "usbhost_bulkTransfer", "");

    usb_transfer_status_t status = usbhost_waitForTransDone(xfer);
    *size = xfer->actual_num_bytes;

    if (status != USB_TRANSFER_STATUS_COMPLETED) {
        ESP_LOGE("usbhost_bulkTransfer", "Transfer fail: %d", status);
        return status;
    }

    if (dir == DEV_TO_HOST) {
        memcpy(data, xfer->data_buffer, xfer->actual_num_bytes);
    }

    return USB_TRANSFER_STATUS_COMPLETED;
}

/* =============== MSC BOT + 基本 SCSI 命令（内联实现） =============== */

#define MSC_CBW_SIG 0x43425355u   /* 'USBC' */
#define MSC_CSW_SIG 0x53425355u   /* 'USBS' */

typedef struct __attribute__((packed)) {
    uint32_t dCBWSignature;
    uint32_t dCBWTag;
    uint32_t dCBWDataTransferLength;
    uint8_t  bmCBWFlags;
    uint8_t  bCBWLUN;
    uint8_t  bCBWCBLength;
    uint8_t  CBWCB[16];
} msc_cbw_t;

typedef struct __attribute__((packed)) {
    uint32_t dCSWSignature;
    uint32_t dCSWTag;
    uint32_t dCSWDataResidue;
    uint8_t  bCSWStatus;
} msc_csw_t;

static uint32_t s_cbw_tag = 1;

/* 发送一条 BOT 命令。data_len=0 也可。dir: DEV_TO_HOST / HOST_TO_DEV */
static esp_err_t msc_bot_command(const uint8_t *cdb, uint8_t cdb_len,
                                 void *data, uint32_t data_len,
                                 usbhost_transDir_t dir, uint32_t timeout_ms)
{
    assert(cdb_len <= 16);
    msc_cbw_t cbw = {0};
    cbw.dCBWSignature         = MSC_CBW_SIG;
    cbw.dCBWTag               = s_cbw_tag++;
    cbw.dCBWDataTransferLength= data_len;
    cbw.bmCBWFlags            = (dir == DEV_TO_HOST) ? 0x80 : 0x00;
    cbw.bCBWLUN               = 0;
    cbw.bCBWCBLength          = cdb_len;
    memcpy(cbw.CBWCB, cdb, cdb_len);

    /* 1) CBW（OUT） */
    uint32_t x = sizeof(msc_cbw_t);
    ESP_RETURN_ON_ERROR(usbhost_bulkTransfer(&cbw, &x, HOST_TO_DEV, timeout_ms),
                        "msc_bot_command: CBW", "");

    /* 2) 数据阶段（可选） */
    if (data_len > 0) {
        x = data_len;
        esp_err_t er = usbhost_bulkTransfer(data, &x, dir, timeout_ms);
        if (er != ESP_OK) {
            ESP_LOGE("msc_bot", "data stage err=%d", er);
            return er;
        }
    }

    /* 3) CSW（IN） */
    msc_csw_t csw = {0};
    x = sizeof(msc_csw_t);
    ESP_RETURN_ON_ERROR(usbhost_bulkTransfer(&csw, &x, DEV_TO_HOST, timeout_ms),
                        "msc_bot_command: CSW", "");

    if (csw.dCSWSignature != MSC_CSW_SIG) {
        ESP_LOGE("msc_bot", "Bad CSW signature: 0x%08x", csw.dCSWSignature);
        return ESP_FAIL;
    }
    if (csw.dCSWTag != cbw.dCBWTag) {
        ESP_LOGE("msc_bot", "Tag mismatch: CBW=%u CSW=%u", cbw.dCBWTag, csw.dCSWTag);
        return ESP_FAIL;
    }
    if (csw.bCSWStatus == 0) return ESP_OK;               /* Passed */
    if (csw.bCSWStatus == 1) return ESP_ERR_INVALID_STATE;/* Failed: 需要 Request Sense */
    return ESP_FAIL;                                       /* Phase Error */
}

/* TEST UNIT READY */
static esp_err_t msc_test_unit_ready(void)
{
    uint8_t cdb[6] = {0x00, 0,0,0,0,0};
    return msc_bot_command(cdb, 6, NULL, 0, DEV_TO_HOST, 5000);
}

/* REQUEST SENSE（取一些可读信息，便于日志） */
static esp_err_t msc_request_sense(uint8_t *sk, uint8_t *asc, uint8_t *ascq)
{
    uint8_t cdb[6] = {0x03, 0, 0, 0, 18, 0};
    uint8_t buf[18] = {0};
    esp_err_t er = msc_bot_command(cdb, 6, buf, sizeof(buf), DEV_TO_HOST, 5000);
    if (sk)   *sk   = buf[2] & 0x0F;
    if (asc)  *asc  = buf[12];
    if (ascq) *ascq = buf[13];
    return er;
}

/* PREVENT/ALLOW MEDIUM REMOVAL */
static esp_err_t msc_prevent_allow_medium_removal(bool prevent)
{
    uint8_t cdb[6] = {0x1E, 0, 0, 0, (uint8_t)(prevent ? 0x01 : 0x00), 0};
    return msc_bot_command(cdb, 6, NULL, 0, DEV_TO_HOST, 5000);
}

/* START STOP UNIT（这里仅启动/停止，不弹仓） */
static esp_err_t msc_start_stop_unit(bool start)
{
    uint8_t cdb[6] = {0x1B, 0, 0, 0, (uint8_t)(start ? 0x01 : 0x00), 0};
    return msc_bot_command(cdb, 6, NULL, 0, DEV_TO_HOST, 8000);
}

/* 自动：上电禁止弹盘→启动马达→轮询就绪 */
static void usbhost_cd_autostart(void)
{
    const char *TAG = "cd_autostart";

    if (xSemaphoreTake(scsiExeLock, pdMS_TO_TICKS(2000)) == pdTRUE) {
        ESP_LOGI(TAG, "Prevent medium removal");
        (void)msc_prevent_allow_medium_removal(true);
        vTaskDelay(pdMS_TO_TICKS(80));

        ESP_LOGI(TAG, "Start spindle");
        (void)msc_start_stop_unit(true);
        xSemaphoreGive(scsiExeLock);
    } else {
        ESP_LOGW(TAG, "scsiExeLock timeout");
    }

    vTaskDelay(pdMS_TO_TICKS(800)); /* 起转缓冲 */

    for (int i = 0; i < 30; i++) { /* ~9 秒 */
        if (xSemaphoreTake(scsiExeLock, pdMS_TO_TICKS(1000)) != pdTRUE) {
            vTaskDelay(pdMS_TO_TICKS(300));
            continue;
        }
        esp_err_t tur = msc_test_unit_ready();
        uint8_t sk=0, asc=0, ascq=0;
        (void)msc_request_sense(&sk,&asc,&ascq);
        xSemaphoreGive(scsiExeLock);

        if (tur == ESP_OK) {
            ESP_LOGI(TAG, "Unit ready");
            break;
        } else {
            ESP_LOGW(TAG, "Not ready yet, sense=%02X/%02X/%02X", sk, asc, ascq);
        }
        vTaskDelay(pdMS_TO_TICKS(300));
    }
}

/* =============== 设备打开/关闭与任务 =============== */

esp_err_t usbhost_openDevice()
{
    /* 打开设备 */
    ESP_LOGI("client_task", "Open device");
    printf("Device addr: %d\n", usbhost_driverObj.dev_addr);

    usb_host_device_open(usbhost_driverObj.handle_client,
                         usbhost_driverObj.dev_addr,
                         &usbhost_driverObj.handle_device);

    /* 读取设备信息 */
    ESP_LOGI("client_task", "Get device information");
    usb_device_info_t dev_info;
    usb_host_device_info(usbhost_driverObj.handle_device, &dev_info);

    printf("USB speed: %s speed\n", (dev_info.speed == USB_SPEED_LOW) ? "Low" : "Full");
    printf("bConfigurationValue: %d\n", dev_info.bConfigurationValue);
    printf("string desc manufacturer: "); usb_print_string_descriptor(dev_info.str_desc_manufacturer);
    printf("string desc product:      "); usb_print_string_descriptor(dev_info.str_desc_product);
    printf("string desc sn:           "); usb_print_string_descriptor(dev_info.str_desc_serial_num);

    /* 设备描述符 */
    ESP_LOGI("client_task", "Get device descriptor");
    const usb_device_desc_t *dev_desc;
    usb_host_get_device_descriptor(usbhost_driverObj.handle_device, &dev_desc);
    usb_print_device_descriptor(dev_desc);

    /* 配置/接口/端点描述符 */
    ESP_LOGI("client_task", "Get config descriptor");
    const usb_config_desc_t *config_desc;
    usb_host_get_active_config_descriptor(usbhost_driverObj.handle_device, &config_desc);
    usb_print_config_descriptor(config_desc, NULL);

    int offset = 0;
    const usb_standard_desc_t *each_desc = (const usb_standard_desc_t *)config_desc;
    while (each_desc != NULL) {
        if (each_desc->bDescriptorType == USB_B_DESCRIPTOR_TYPE_INTERFACE) {
            usbhost_driverObj.desc_interface = (usb_intf_desc_t *)each_desc;
        } else if (each_desc->bDescriptorType == USB_B_DESCRIPTOR_TYPE_ENDPOINT) {
            uint8_t epAddr = ((usb_ep_desc_t *)each_desc)->bEndpointAddress;
            uint8_t type   = ((usb_ep_desc_t *)each_desc)->bmAttributes;
            if (epAddr & 0x80) {              /* IN */
                if ((type & 0x3) == 2) usbhost_driverObj.desc_ep_in  = (usb_ep_desc_t *)each_desc;
            } else {                           /* OUT */
                if ((type & 0x3) == 2) usbhost_driverObj.desc_ep_out = (usb_ep_desc_t *)each_desc;
            }
        }
        if (usbhost_driverObj.desc_interface && usbhost_driverObj.desc_ep_out && usbhost_driverObj.desc_ep_in) break;
        each_desc = usb_parse_next_descriptor(each_desc, config_desc->wTotalLength, &offset);
    }

    /* 判断是否 MSC Bulk-Only */
    if (usbhost_driverObj.desc_interface->bInterfaceClass == 0x08 &&
        ((usbhost_driverObj.desc_interface->bInterfaceSubClass == 0x05) ||
         (usbhost_driverObj.desc_interface->bInterfaceSubClass == 0x06) ||
         (usbhost_driverObj.desc_interface->bInterfaceSubClass == 0x02)) &&
        usbhost_driverObj.desc_interface->bInterfaceProtocol == 0x50) {
        printf("USB Mass Storage Class Bulk-Only device\n");
    } else {
        printf("Not BBB device\n");
        return ESP_FAIL;
    }

    /* 记录端点信息 */
    if (usbhost_driverObj.desc_ep_out == NULL || usbhost_driverObj.desc_ep_in == NULL) {
        printf("Endpoint descriptor not found.\n");
        return ESP_FAIL;
    }
    usbhost_driverObj.ep_in_num       = usbhost_driverObj.desc_ep_in->bEndpointAddress;  /* 不 &0x0f */
    usbhost_driverObj.ep_in_packsize  = usbhost_driverObj.desc_ep_in->wMaxPacketSize;
    usbhost_driverObj.ep_out_num      = usbhost_driverObj.desc_ep_out->bEndpointAddress;
    usbhost_driverObj.ep_out_packsize = usbhost_driverObj.desc_ep_out->wMaxPacketSize;
    printf("ep in:%d, packsize:%d\n",  usbhost_driverObj.ep_in_num,  usbhost_driverObj.ep_in_packsize);
    printf("ep out:%d, packsize:%d\n", usbhost_driverObj.ep_out_num, usbhost_driverObj.ep_out_packsize);

    /* 分配传输对象 */
    esp_err_t err = usb_host_transfer_alloc(usbhost_driverObj.ep_out_packsize, 0, &usbhost_driverObj.transferObj);
    if (err != ESP_OK) {
        printf("usb_host_transfer_alloc fail\n");
        return ESP_FAIL;
    }

    /* 声明接口 */
    usb_host_interface_claim(usbhost_driverObj.handle_client,
                             usbhost_driverObj.handle_device,
                             usbhost_driverObj.desc_interface->bInterfaceNumber,
                             usbhost_driverObj.desc_interface->bAlternateSetting);

    /* 给设备一点初始化时间 */
    vTaskDelay(pdMS_TO_TICKS(800));
    usbhost_driverObj.deviceIsOpened = 1;

    /* 自动：禁止弹盘 + 启转 + 轮询就绪 */
    usbhost_cd_autostart();

    return ESP_OK;
}

void usbhost_closeDevice()
{
    if (usbhost_driverObj.handle_device == NULL) return;

    usb_host_interface_release(usbhost_driverObj.handle_client,
                               usbhost_driverObj.handle_device,
                               usbhost_driverObj.desc_interface->bInterfaceNumber);
    usb_host_device_close(usbhost_driverObj.handle_client, usbhost_driverObj.handle_device);
    usb_host_transfer_free(usbhost_driverObj.transferObj);

    usbhost_driverObj.handle_device   = NULL;
    usbhost_driverObj.desc_interface  = NULL;
    usbhost_driverObj.desc_ep_out     = NULL;
    usbhost_driverObj.desc_ep_in      = NULL;
    usbhost_driverObj.dev_addr        = 0;
    usbhost_driverObj.ep_in_num       = 0;
    usbhost_driverObj.ep_in_packsize  = 0;
    usbhost_driverObj.ep_out_num      = 0;
    usbhost_driverObj.ep_out_packsize = 0;
    usbhost_driverObj.deviceIsOpened  = 0;
}

static void usbhost_cb_client(const usb_host_client_event_msg_t *event_msg, void *arg)
{
    usbhost_driver_t *obj = (usbhost_driver_t *)arg;
    switch (event_msg->event) {
    case USB_HOST_CLIENT_EVENT_NEW_DEV:
        if (event_msg->new_dev.address != 0 && obj->handle_device == NULL) {
            obj->dev_addr = event_msg->new_dev.address;
            senMsgToClientTask(CLASS_DRIVER_ACTION_NEW_DEV);
        }
        break;
    case USB_HOST_CLIENT_EVENT_DEV_GONE:
        if (obj->handle_device != NULL) senMsgToClientTask(CLASS_DRIVER_ACTION_CLOSE_DEV);
        break;
    default:
        abort();
    }
}

static void usbhost_task_client(void *arg)
{
    queue_client = xQueueCreate(10, sizeof(uint8_t));

    ESP_LOGI("client_task", "Registering Client");
    usb_host_client_config_t client_config = {
        .is_synchronous     = false,
        .max_num_event_msg  = 5,
        .async = {
            .client_event_callback = usbhost_cb_client,
            .callback_arg          = (void *)&usbhost_driverObj,
        },
    };
    ESP_ERROR_CHECK(usb_host_client_register(&client_config, &usbhost_driverObj.handle_client));

    uint8_t msg;
    while (1) {
        /* 阻塞等待事件；事件到达进入回调，然后解除阻塞执行队列动作 */
        usb_host_client_handle_events(usbhost_driverObj.handle_client, portMAX_DELAY);

        BaseType_t queue_ret = xQueueReceive(queue_client, &msg, 0);
        if (queue_ret == pdTRUE) {
            switch (msg) {
            case CLASS_DRIVER_ACTION_NEW_DEV: {
                printf("USB device connected.\n");
                esp_err_t ret = usbhost_openDevice();
                if (ret == ESP_FAIL) usbhost_closeDevice();
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
}

static void usbhost_task_usblibDaemon(void *arg)
{
    while (1) {
        uint32_t event_flags;
        ESP_ERROR_CHECK(usb_host_lib_handle_events(portMAX_DELAY, &event_flags));
        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
            ESP_LOGI("usbhost_task_usblibDaemon", "USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS");
        }
        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE) {
            ESP_LOGI("usbhost_task_usblibDaemon", "USB_HOST_LIB_EVENT_FLAGS_ALL_FREE");
        }
    }
}

void usbhost_driverInit()
{
    /* 安装主机库 */
    ESP_LOGI("usbhost_driverInit", "Installing USB Host Library");
    usb_host_config_t host_config = {
        .skip_phy_setup = false,
        .intr_flags     = ESP_INTR_FLAG_LEVEL1,
        .enum_filter_cb = NULL,
    };
    ESP_ERROR_CHECK(usb_host_install(&host_config));

    BaseType_t ret;

    /* usb 主机库守护进程 */
    ret = xTaskCreatePinnedToCore(usbhost_task_usblibDaemon,
                                  "usbhost_task_usblibDaemon",
                                  4096, NULL, DAEMON_TASK_PRIORITY, NULL, 1);
    if (ret != pdPASS) ESP_LOGE("usbhost_driverInit", "usbhost_task_usblibDaemon create fail");

    /* client 进程 */
    ret = xTaskCreatePinnedToCore(usbhost_task_client,
                                  "usbhost_task_client",
                                  4096, NULL, CLIENT_TASK_PRIORITY, NULL, 1);
    if (ret != pdPASS) ESP_LOGE("usbhost_driverInit", "usbhost_task_client create fail");

    vTaskDelay(10); /* 让 client 起来 */

    usbhost_driverObj.transferDone = xSemaphoreCreateBinary();
    scsiExeLock = xSemaphoreCreateMutex();
}
