#include "device_manager.h"
#include "memory.h"

static RootDevice devices[ROOT_DEVICE_MAX];
static usize device_count = 0u;

static const RootDriver* drivers[ROOT_DRIVER_MAX];
static usize driver_count = 0u;

static bool driver_matches(
    const RootDriver* driver,
    const RootDevice* device
)
{
    if (driver == NULL || device == NULL)
    {
        return false;
    }

    if (device->bus != ROOT_DEVICE_BUS_PCI)
    {
        return false;
    }

    const RootDriverMatch* match = &driver->match;
    const PciDevice* pci = &device->pci;

    if (
        match->vendor_id != ROOT_DRIVER_ANY_VENDOR
        &&
        match->vendor_id != pci->vendor_id
    )
    {
        return false;
    }

    if (
        match->device_id != ROOT_DRIVER_ANY_DEVICE
        &&
        match->device_id != pci->device_id
    )
    {
        return false;
    }

    if (
        match->class_code != ROOT_DRIVER_ANY_CLASS
        &&
        match->class_code != pci->class_code
    )
    {
        return false;
    }

    if (
        match->subclass != ROOT_DRIVER_ANY_CLASS
        &&
        match->subclass != pci->subclass
    )
    {
        return false;
    }

    if (
        match->prog_if != ROOT_DRIVER_ANY_CLASS
        &&
        match->prog_if != pci->prog_if
    )
    {
        return false;
    }

    return true;
}

static void device_manager_import_pci(void)
{
    usize pci_count = pci_device_count();

    for (
        usize i = 0u;
        i < pci_count && device_count < ROOT_DEVICE_MAX;
        i++
    )
    {
        PciDevice pci_device;

        if (!pci_get_device(i, &pci_device))
        {
            continue;
        }

        RootDevice* device = &devices[device_count];
        root_memzero(device, sizeof(*device));

        device->id = (u32)device_count;
        device->bus = ROOT_DEVICE_BUS_PCI;
        device->driver_state = ROOT_DEVICE_DRIVER_UNBOUND;
        device->driver_name = NULL;
        device->display_name =
            pci_subclass_name(
                pci_device.class_code,
                pci_device.subclass,
                pci_device.prog_if
            );
        root_memcpy(
            &device->pci,
            &pci_device,
            sizeof(device->pci)
        );

        device_count++;
    }
}

void device_manager_rescan(void)
{
    /*
     * Give bound drivers a chance to stop hardware and release child state
     * before rebuilding the PCI inventory.  v0.44 needs this for xHCI: a
     * controller reset must not leave stale USB slots behind.
     */
    for (usize i = 0u; i < device_count; i++)
    {
        RootDevice* device = &devices[i];

        if (
            device->driver_state != ROOT_DEVICE_DRIVER_BOUND
            ||
            device->driver_name == NULL
        )
        {
            continue;
        }

        for (usize d = 0u; d < driver_count; d++)
        {
            const RootDriver* driver = drivers[d];

            if (
                driver == NULL
                ||
                driver->name == NULL
                ||
                driver->detach == NULL
                ||
                driver->name != device->driver_name
            )
            {
                continue;
            }

            driver->detach(device);
            break;
        }
    }

    root_memzero(devices, sizeof(devices));
    device_count = 0u;

    pci_rescan();
    device_manager_import_pci();
    device_manager_bind_drivers();
}

void device_manager_init(void)
{
    root_memzero(devices, sizeof(devices));
    root_memzero(drivers, sizeof(drivers));
    device_count = 0u;
    driver_count = 0u;

    device_manager_import_pci();
}

usize device_manager_count(void)
{
    return device_count;
}

bool device_manager_get(usize index, RootDevice* output)
{
    if (output == NULL || index >= device_count)
    {
        return false;
    }

    root_memcpy(
        output,
        &devices[index],
        sizeof(*output)
    );

    return true;
}

bool device_manager_find_pci_class(
    u8 class_code,
    u8 subclass,
    u8 prog_if,
    usize start_index,
    usize* found_index,
    RootDevice* output
)
{
    for (usize i = start_index; i < device_count; i++)
    {
        RootDevice* device = &devices[i];

        if (
            device->bus != ROOT_DEVICE_BUS_PCI
            ||
            device->pci.class_code != class_code
            ||
            device->pci.subclass != subclass
            ||
            device->pci.prog_if != prog_if
        )
        {
            continue;
        }

        if (found_index != NULL)
        {
            *found_index = i;
        }

        if (output != NULL)
        {
            root_memcpy(
                output,
                device,
                sizeof(*output)
            );
        }

        return true;
    }

    return false;
}

bool device_manager_register_driver(const RootDriver* driver)
{
    if (
        driver == NULL
        ||
        driver->name == NULL
        ||
        driver->name[0] == '\0'
        ||
        driver->attach == NULL
        ||
        driver_count >= ROOT_DRIVER_MAX
    )
    {
        return false;
    }

    for (usize i = 0u; i < driver_count; i++)
    {
        if (drivers[i] == driver)
        {
            return true;
        }
    }

    drivers[driver_count++] = driver;
    return true;
}


bool device_manager_mark_external_bound(u32 device_id, const char* driver_name)
{
    if (driver_name == NULL || driver_name[0] == '\0' || device_id >= device_count)
        return false;
    RootDevice* device = &devices[device_id];
    device->driver_state = ROOT_DEVICE_DRIVER_BOUND;
    device->driver_name = driver_name;
    return true;
}

bool device_manager_mark_external_failed(u32 device_id, const char* driver_name)
{
    if (driver_name == NULL || driver_name[0] == '\0' || device_id >= device_count)
        return false;
    RootDevice* device = &devices[device_id];
    device->driver_state = ROOT_DEVICE_DRIVER_FAILED;
    device->driver_name = driver_name;
    return true;
}

void device_manager_bind_drivers(void)
{
    for (usize device_index = 0u; device_index < device_count; device_index++)
    {
        RootDevice* device = &devices[device_index];

        if (device->driver_state == ROOT_DEVICE_DRIVER_BOUND)
        {
            continue;
        }

        device->driver_state = ROOT_DEVICE_DRIVER_UNBOUND;
        device->driver_name = NULL;

        for (usize driver_index = 0u; driver_index < driver_count; driver_index++)
        {
            const RootDriver* driver = drivers[driver_index];

            if (!driver_matches(driver, device))
            {
                continue;
            }

            if (
                driver->probe != NULL
                &&
                !driver->probe(device)
            )
            {
                continue;
            }

            if (driver->attach(device))
            {
                device->driver_state = ROOT_DEVICE_DRIVER_BOUND;
                device->driver_name = driver->name;
                break;
            }

            device->driver_state = ROOT_DEVICE_DRIVER_FAILED;
            device->driver_name = driver->name;
            break;
        }
    }
}

usize device_manager_driver_count(void)
{
    return driver_count;
}

bool device_manager_get_driver(usize index, const RootDriver** output)
{
    if (output == NULL || index >= driver_count)
    {
        return false;
    }

    *output = drivers[index];
    return true;
}

const char* device_manager_bus_name(RootDeviceBus bus)
{
    switch (bus)
    {
        case ROOT_DEVICE_BUS_PCI: return "PCI";
        default: return "unknown";
    }
}

const char* device_manager_driver_state_name(RootDeviceDriverState state)
{
    switch (state)
    {
        case ROOT_DEVICE_DRIVER_BOUND: return "bound";
        case ROOT_DEVICE_DRIVER_FAILED: return "failed";
        default: return "unbound";
    }
}
