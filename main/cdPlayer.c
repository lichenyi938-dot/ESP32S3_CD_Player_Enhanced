// ===== put this near the top of cdPlayer.c =====
#include "esp_log.h"

// 声明我们需要用到的 SCSI 命令（工程里已有实现）
extern esp_err_t msc_inquiry(void *buf, size_t len);
extern esp_err_t msc_read_toc(void *buf, size_t len);        // READ TOC/PMA/ATIP, Format 0
extern esp_err_t msc_test_unit_ready(void);
extern esp_err_t msc_request_sense(uint8_t *buf, size_t len);

static const char *TAG_CDCHK = "cd_detect";

// 兼容“桥接误报”的光驱判定：
// 1) INQUIRY 的 PDT==0x05 直接判定为光驱
// 2) 否则尝试 READ TOC；成功则也视为光驱
static bool cd_is_optical_or_probably(void)
{
    uint8_t inq[36] = {0};
    if (msc_inquiry(inq, sizeof(inq)) != ESP_OK) {
        ESP_LOGW(TAG_CDCHK, "INQUIRY failed, can't decide.");
        return false;
    }
    uint8_t pdt = inq[0] & 0x1F;          // Peripheral Device Type

    if (pdt == 0x05) {                    // CD/DVD
        ESP_LOGI(TAG_CDCHK, "PDT=0x05 -> optical drive");
        return true;
    }

    // 某些桥把 ODD 误报为硬盘 (0x00)。试探 READ TOC：
    uint8_t toc_hdr[12] = {0};
    if (msc_read_toc(toc_hdr, sizeof(toc_hdr)) == ESP_OK) {
        ESP_LOGW(TAG_CDCHK,
                 "Bridge misreports PDT=0x%02X, READ TOC OK -> treat as optical.", pdt);
        return true;
    }

    ESP_LOGI(TAG_CDCHK, "Not optical (PDT=0x%02X) and READ TOC failed.", pdt);
    return false;
}

// ===== 在你的设备/光盘监控流程里，把原先“是不是光驱”的判断整段替换为： =====
// 伪代码示例：在你打印 “Check if usb device is CD/DVD device” 的地方，改成：
/*
ESP_LOGI(TAG, "Check if usb device is CD/DVD device...");
bool is_cd = cd_is_optical_or_probably();
if (!is_cd) {
    // 不是/探测失败：仅提示，但不要 return，让下次插拔还能再试
    ESP_LOGI(TAG, "Not CD/DVD device (or bridge blocked MMC).");
    // 这里保持原有逻辑：等待拔插/下一轮循环
} else {
    // 是光驱：进入原来的就绪等待、READ TOC、播放等流程
    // 建议：先 test unit ready，不就绪时发 request sense 解锁，之后再读 TOC/启动播放
}
*/
