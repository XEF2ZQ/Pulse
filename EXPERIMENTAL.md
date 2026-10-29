# Pulse experimental line

This branch continues from preserved 1.4.4-platform-idle-preview. The current
candidate is **1.5.0-experimental**, with battery-only adaptive scheduling.

New scheduling controls remain off until explicitly enabled. This is not the
installed standard build and must not silently replace its startup entry.
See [branch policy](BRANCHES.md), [implementation](docs/adaptive-experimental.md)
and [evidence](docs/evidence/2026-10-29-adaptive/README.md).

The earlier platform observer remains available. No new firmware-level PSR,
DF/GFXOFF, undervolt or SMU power-limit actuator is claimed.
