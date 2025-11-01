#ifndef __BT_A2DP_H__
#define __BT_A2DP_H__

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void bt_a2dp_init(void);
bool bt_is_active(void);
void bt_a2dp_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif