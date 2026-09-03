#ifndef IMPL_ADV_API_DETAIL_HCOMM_IMPL_HCOMM_JETTY_IMPL_DEF_H
#define IMPL_ADV_API_DETAIL_HCOMM_IMPL_HCOMM_JETTY_IMPL_DEF_H

#include "../common/hcomm_inner_def.h"

#if defined(__NPU_ARCH__) && __NPU_ARCH__ == 3510
#include "platform_v310/hcomm_aiv_urma_jetty.h"
#endif

#endif
