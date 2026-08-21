#include "net_device.h"
#include "memory.h"
#include "string.h"

static RootNetDevice devices[ROOT_NET_DEVICE_MAX];
static usize count = 0u;
static usize active_index = 0u;
static bool active_valid = false;

void net_device_init(void)
{
    root_memzero(devices, sizeof(devices));
    count = 0u;
    active_index = 0u;
    active_valid = false;
}

bool net_device_register(
    const char* driver_name,
    void* context,
    const RootNetDeviceOps* ops,
    const u8 mac[6]
)
{
    if (
        driver_name == NULL || driver_name[0] == '\0' ||
        ops == NULL || ops->send_frame == NULL || ops->receive_frame == NULL ||
        ops->ready == NULL || ops->link_up == NULL || mac == NULL ||
        count >= ROOT_NET_DEVICE_MAX
    )
    {
        return false;
    }

    RootNetDevice* device = &devices[count];
    root_memzero(device, sizeof(*device));
    device->used = true;
    device->id = (u32)count;
    device->driver_name = driver_name;
    device->context = context;
    root_memcpy(&device->ops, ops, sizeof(device->ops));
    root_memcpy(device->mac, mac, 6u);

    if (!active_valid)
    {
        active_index = count;
        active_valid = true;
    }

    count++;
    return true;
}

usize net_device_count(void)
{
    return count;
}

bool net_device_get(usize index, RootNetDevice* output)
{
    if (output == NULL || index >= count)
        return false;
    root_memcpy(output, &devices[index], sizeof(*output));
    return true;
}

const RootNetDevice* net_device_active(void)
{
    if (!active_valid || active_index >= count)
        return NULL;
    return &devices[active_index];
}

bool net_device_select(usize index)
{
    if (index >= count || !devices[index].used)
        return false;

    active_index = index;
    active_valid = true;
    return true;
}

bool net_device_active_index(usize* output)
{
    if (output == NULL || !active_valid || active_index >= count)
        return false;

    *output = active_index;
    return true;
}

bool net_device_is_active(usize index)
{
    return active_valid && index < count && index == active_index;
}

const char* net_device_active_driver(void)
{
    const RootNetDevice* device = net_device_active();
    return device != NULL ? device->driver_name : "none";
}

bool net_device_ready(void)
{
    const RootNetDevice* device = net_device_active();
    return device != NULL && device->ops.ready(device->context);
}

bool net_device_link_up(void)
{
    const RootNetDevice* device = net_device_active();
    return device != NULL && device->ops.link_up(device->context);
}

bool net_device_send_frame(const void* data, usize size)
{
    const RootNetDevice* device = net_device_active();
    return device != NULL && device->ops.send_frame(device->context, data, size);
}

bool net_device_receive_frame(void* output, usize capacity, usize* result_size)
{
    const RootNetDevice* device = net_device_active();
    return device != NULL && device->ops.receive_frame(device->context, output, capacity, result_size);
}

void net_device_get_mac(u8 output[6])
{
    if (output == NULL)
        return;
    root_memzero(output, 6u);
    const RootNetDevice* device = net_device_active();
    if (device != NULL)
        root_memcpy(output, device->mac, 6u);
}
