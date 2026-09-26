# Publication preparation

## Current status

- GitHub profile access confirmed for `Vanebo`.
- The supplied `kiln_PIDcontroller_v04.zip` working project was imported without generated `.pio` output or editor metadata.
- The imported firmware identifies itself as 0.4.1 and has real heater output enabled.
- The accessible conversation and project documentation provide only partial v0.1–v0.3 history.
- A clean PlatformIO release build completed successfully on 2026-09-26. Hardware and OTA behavior were not retested during publication preparation.
- MIT licensing was selected for the original project source and documentation. The Inter-based embedded font attribution is recorded separately.
- GitHub publication and release creation remain pending.

## Imported working version

The original ZIP remains outside this repository. The PlatformIO configuration, partition table and top-level files from `src/` were copied into the public repository structure. Generated build files, downloaded dependencies, `.vscode` state and `.qodo` metadata were excluded.

Before publishing, inspect source and assets for credentials or personal runtime data. Keep required defaults and sample programs; exclude real credentials, filesystem dumps and build caches. Document any necessary redaction instead of silently changing firmware behavior.

The README now records the board, display, sensor, pin assignments, 0–10 V control path, initial build/upload process and storage layout. Add the owner's schematic and BOM links before declaring the hardware documentation complete.

## Version history

Import v0.4 as the `main` version. Keep v0.1–v0.3 history in the changelog unless authentic earlier snapshots become available. Never tag the v0.4 commit as older firmware. If older archives arrive, establish their exact contents and versions before creating historical tags or releases.

## Publish

After importing and reviewing the source, use the chosen GitHub repository. If it already exists, inspect its contents and preserve its history. Commit the complete reviewed project to `main`; do not overwrite unrelated work.

The `esp32-s3` environment builds successfully with Espressif32 platform 7.1.3, Arduino-ESP32 framework package 4.20017.260907, ArduinoJson 6.21.6 and Adafruit MAX31855 1.4.2. The build used 92,456 bytes of RAM (28.2%) and 971,497 bytes of application flash (15.4% of the 6,291,456-byte app partition). Test the resulting firmware on the target hardware before claiming hardware or OTA validation.

Create tag `v0.4.1` only at the commit containing this imported working firmware. Use `docs/releases/v0.4.1.md` as the release body after updating its verification status. Attach a verified application image only if built from that tagged commit; include its checksum and board/build configuration. GitHub's source archive is not a precompiled firmware image.

Do not publish the placeholder historical notes as actual releases until their corresponding source and history are recovered.
