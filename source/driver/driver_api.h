#ifndef ROOTOS_DRIVER_API_H
#define ROOTOS_DRIVER_API_H

#include "types.h"
#include "device_manager.h"
#include <rootos/rootdriver.h>

void driver_api_init(void);
const RootDriverApi* driver_api(void);
void driver_api_make_device(const RootDevice* source, RootDriverDeviceInfo* output);

#endif
