# gdolib

Garage door opener library for controlling security+ version 1.0 and 2.0 garage door openers for ESP32 microcontrollers.

Door commands supersede earlier timed moves and delayed toggle sequences. A failed replacement preserves the previous partial move's safety stop. Stop cancels a timed start that has not reached the UART, and stops an already transmitted timed start even when opener state feedback is still pending. Required button releases continue after cancellation.


## Prerequisities:
* ESP-IDF v4.4+

## Building:
* Setup esp-idf, see instructions [here](https://docs.espressif.com/projects/esp-idf/en/v5.2.1/esp32/get-started/index.html).
* Follow the directions [here](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/tools/idf-py.html) to create a new project and copy the files from the examples folder in this repo into the new project folder.
* cd into the folder, run <path-to-esp-idf>/export.sh if necessary to setup the environment.
* run `idf.py  build`

At this point it should have successfully built the project and the output will be in the build folder inside the project folder.
To extract the library, navigate to `build/esp-idf/gdolib` and copy the `libgdolib.a` file to another project folder where you want it to be statically linked.

## Releases:
Bump `version` in `library.json` using stable `X.Y.Z` numbering. When the change reaches `master`, the release workflow runs the host and contract tests, creates the matching `vX.Y.Z` tag at that commit, and publishes a GitHub release with generated notes and source archives. Unchanged versions and existing published releases are skipped. An existing draft release or a tag pointing to a different commit stops publication for review.

## Credits:
This library incorporates secplus by Clayton Smith https://github.com/argilo/secplus
