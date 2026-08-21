# RootOs v0.47.6-3 (UNSTABLE)

This is the third repair of the RootOS 0.47.6 development version.

## What this repair fixes

### 1. Brute-force framebuffer performance fix

RootOS already keeps a logical terminal cell buffer and already batches command
output. It also has incremental scrollback rendering. The remaining expensive
path was lower down: scrolling and cursor background operations were reading
and moving the physical GOP framebuffer itself.

On real hardware, framebuffer reads can be dramatically slower than cached RAM.

v0.47.6-3 therefore adds a 16 MiB cached shadow framebuffer:

    terminal / cursor / selection
               |
               v
        normal cached RAM
               |
        damage rectangle
               |
               v
      sequential WC writes
               |
               v
         physical GOP FB

The established RootDisplay renderer remains the backend. Its `framebuffer`
pointer is redirected to cached RAM whenever the mode fits the shadow buffer.

At the end of a RootDisplay update transaction, only the accumulated damaged
rectangle is copied to the real framebuffer.

This combines the two useful strategies already used by mature/open systems:

- logical console operations and batched scrolling instead of repainting the
  whole terminal for every character;
- buffered/deferred rendering so slow device memory is primarily written
  sequentially instead of read/modified constantly.

The previous PAT Write-Combining mapping remains enabled when supported.

Modes larger than the 16 MiB shadow buffer do not fail to boot; they fall back
to the previous direct renderer.

### 2. Software mouse cursor is no longer conditional on PS/2

Previously RootInput disabled the graphical pointer and re-enabled it only if
the PS/2 mouse initialization succeeded.

That means a modern USB-only PC could have a perfectly working framebuffer but
no cursor would be drawn at all.

v0.47.6-3 always displays the RootOS software cursor when RootDisplay is ready.
PS/2 remains a movement backend when available.

USB HID movement is a separate feature layer planned for the 0.48 line.

### 3. USB host controller actually starts in normal boot

The current xHCI driver separates discovery from hardware start:

    xhci_init()   -> discover/register controller
    xhci_start()  -> reset/start controller and enumerate root ports

Normal boot previously called only `xhci_init()`. The shell could poll USB
forever, but there was no running controller until the user manually issued
`usb start`.

v0.47.6-3 starts xHCI automatically after USB Mass Storage and RNDIS listeners
are registered.

The start is non-fatal:

- no xHCI controller -> continue boot;
- xHCI initialization failure -> continue boot;
- at least one running controller -> enumerate USB and service deferred device
  notifications.

Safe mode still skips hardware discovery.

## Important current limits

This repair does NOT pretend that all USB hardware support is complete.

Current remaining architectural work includes:

- USB HID keyboard/mouse Interrupt-IN endpoints;
- HID Boot Protocol and later generic HID report parsing;
- USB hub traversal;
- xHCI BARs above 4 GiB through a generic RootIO/ioremap layer;
- EHCI/UHCI/OHCI for older USB-only machines.

Those are functionality additions and belong to the 0.48 development line,
rather than another 0.47.6 repair suffix.

## Build

```bash
make clear
make
make check
make run
```

## Physical USB update

```bash
lsblk -o NAME,PATH,TYPE,SIZE,MODEL,TRAN,MOUNTPOINTS
make usb-update DEVICE=/dev/sdX
```

Use the whole USB disk, not a partition.
