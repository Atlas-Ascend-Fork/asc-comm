/*
 * The ASC translation unit owns the DFX host implementation and the queue-reader
 * AIV kernel.  Consumers include only the declaration header and link the
 * resulting shared library.
 */
#include "hcomm/dfx/hcomm_channel_dump.h"
#include "host/hcomm_dfx.h"
