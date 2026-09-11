/*
 * The ASC translation unit owns the DFX host implementation and the queue-reader
 * AIV kernel.  Consumers include only the declaration header and link the
 * resulting shared library.
 */
#define HCOMM_INCLUDE_INTERNAL_HEADERS
#include "kernel_operator.h"
#include "hcomm/dfx/hcomm_channel_dump.h"
#include "host/hcomm_dfx.h"
