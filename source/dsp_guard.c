#include <3ds.h>
#include <stdio.h>

/*
 * Check the DSP component before entering the application's real main().
 * libctru loads /3ds/dspfirm.cdc from the SD card when no component was
 * supplied by the launcher. If it is missing, ndspInit() returns a DSP/
 * NOTFOUND result.
 */
extern int __real_main(int argc, char **argv);

int __wrap_main(int argc, char **argv)
{
    Result rc = ndspInit();

    if (R_FAILED(rc)) {
        gfxInitDefault();
        consoleInit(GFX_TOP, NULL);

        printf("\x1b[14;13Hdsp firmware not dumped");
        printf("\x1b[16;8HDump the DSP firmware and try again.");
        printf("\x1b[29;13HPress START to exit.");

        while (aptMainLoop()) {
            hidScanInput();

            if (hidKeysDown() & KEY_START)
                break;

            gfxFlushBuffers();
            gfxSwapBuffers();
            gspWaitForVBlank();
        }

        gfxExit();
        return 0;
    }

    /* The real application also initializes NDSP for its UI sounds. */
    int result = __real_main(argc, argv);

    /* Balance the guard's reference to NDSP after the application exits. */
    ndspExit();
    return result;
}
