# RootOs v0.47-5-1 (UNSTABLE)

RootOs is a free, open-source meta-operating system designed from scratch as a structural foundation to help you build and deploy your own custom operating system.

Currently, the custom kernel runs reliably inside the **QEMU** emulator. Native support and boot stability for physical bare-metal hardware (both legacy BIOS and modern UEFI) are actively under development and targeted for the upcoming **v0.5** release.

---

## Prerequisites (Development Environment)

To compile the codebase and emulate RootOs, you will need a Linux-based environment (Debian/Ubuntu preferred) with the following toolchain dependencies installed:

```bash
sudo apt update
sudo apt install build-essential qemu-system-x86 grub-pc-bin xorriso mtools
```

---

##  Compilation Commands & Toolchain Usage

The project utilizes an automated `Makefile` system. Open a terminal in the root folder of RootOs and execute the commands matching your target workflow:

### 1. Rapid Deployment (Emulation)
Compiles the kernel binary, constructs the initial root filesystem tree, builds the bootable ISO, and triggers the QEMU emulator automatically:
```bash
make run
```

### 2. Recovery & Diagnostics Cycle (Troubleshooting)
If you perform deep architectural changes or modifications to the linker script and the emulator hangs or encounters errors, execute this structural sequence to purge build artifacts and enforce a clean state:
```bash
make clean    # Removes intermediate object (.o) compilation files
make clear    # Purges residual build binaries and temporary image directories
make          # Executes a complete, fresh compilation of the source tree
make check    # Verifies Multiboot header alignment and ELF structure integrity
make run      # Boots the validated kernel image back into the emulator
```

---

##  Repository Structure

*   `/source`: Main Kernel codebase containing assembly initialization and core C/Rust logic.
*   `/drivers`: Native low-level device drivers (Keyboard, Display, and Network subsystems under development).
*   `/grub`: Configuration parameters for the stage-2 bootloader (`grub.cfg`).
*   `/rootfs`: The baseline user-space filesystem structure mounted alongside the kernel.
*   `/sdk`: Software Development Kit headers and interfaces for native userspace apps.
*   `/examples`: Functional blueprint applications (such as `/hello`) to test execution environments.

---

##  Roadmap & WIP (Work In Progress)

*   [ ] **v0.5 (Next Release):** Implement hybrid ISO structures for stable bare-metal boot on legacy/modern PCs.
*   [ ] **WIP:** Automated packages builder and native software installer framework.
*   [ ] **WIP:** Finalizing stable native network stack and driver architecture.

---

##  License

This project is released under the **CC0 1.0 Universal (Creative Commons Zero)** Public Domain Dedication. You can copy, modify, distribute, and build upon the software, even for commercial purposes, completely free of restrictions without requiring prior permission or attribution.
