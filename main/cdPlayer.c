/**************  cdPlayer.c includes (覆盖整个头部)  **************/
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>      // for true/false/bool
#include "esp_err.h"      // for ESP_OK
#include "esp_log.h"

/* 你已有的头文件继续保留 */
#include "cdPlayer.h"
#include "gui_cdPlayer.h"

/* MSC/USB Host 相关：优先用组件里的声明；没有就用兜底原型 */
#include "usbhost_driver.h"

/* 若你的组件里有下面这个头，请保留；如果没有，下面的“兜底原型”会生效 */
#if __has_include("usbhost_msc_cmd.h")
#  include "usbhost_msc_cmd.h"
#endif

/* ---- 兜底原型：防止 msc_* 系列出现“隐式声明”报错 ----
   如果你的项目里已经有 usbhost_msc_cmd.h，并且这些函数有声明，
   这些原型会被同名的真实声明覆盖，不会有副作用。 */
#ifndef MSC_FUNCS_DECLARED
#define MSC_FUNCS_DECLARED 1
#ifdef __cplusplus
extern "C" {
#endif
esp_err_t msc_inquiry(void *buf, size_t len);              // INQUIRY
esp_err_t msc_read_toc(void *toc_hdr_buf, size_t len);     // READ TOC
/* 如后面还有 msc_test_unit_ready / msc_request_sense / msc_start_stop_unit 等，
   一并在这里按需补上原型也可：
// esp_err_t msc_test_unit_ready(void);
// esp_err_t msc_request_sense(void *buf, size_t len);
// esp_err_t msc_start_stop_unit(bool start, bool load_eject);
*/
#ifdef __cplusplus
}
#endif
#endif /* MSC_FUNCS_DECLARED */

/* 某些版本工具链在开启 -Werror 时，未使用的静态函数会当成错误，这里统一屏蔽 */
#pragma GCC diagnostic ignored "-Wunused-function"
/**************  end of cdPlayer.c includes  **************/
