# HundeOS Assets

This directory contains project-owned source artwork and other non-generated
media. Assets here are retained for documentation, boot visuals, release
material, and integration into HundeOS as those uses are implemented. They are
not disposable build artifacts.

## Inventory

- `logo.png` is the primary HundeOS launch artwork for framebuffer consoles.
- `logo.ascii` is the launch artwork for systems without a framebuffer.
- `panic.png` is the HundeOS kernel-panic artwork for framebuffer consoles.
- `panic.ascii` is the kernel-panic artwork for systems without a framebuffer.

The build converts the PNG sources into fixed-size RGB data embedded in the
kernel. The kernel scales and centres that data at runtime; it does not parse PNG
or depend on a filesystem or userspace service during launch or panic handling.

Do not delete, replace, optimise, rename, or otherwise modify these files as
part of repository cleanup or unrelated work. Asset changes require an explicit
request describing the intended change.
