Beanjar CLI preview — Apple Silicon, macOS 26 or later

1. Compare the image's SHA-256 with the distributor's published checksum.
2. Drag Beanjar.app onto Applications. Open Beanjar from Applications.
3. If macOS blocks the first launch because the app is not notarized, and
   you trust the download, go to System Settings > Privacy & Security,
   choose Open Anyway for Beanjar, and confirm.

Beanjar opens a native QEMU window for the included test guest. Terminal
shows boot diagnostics. Close the QEMU window to stop the guest.
The test guest does not need any TV firmware.

To use the CLI directly:

    /Applications/Beanjar.app/Contents/MacOS/Beanjar --help

This preview does not include the webOS TV interface or Apple notarization.
