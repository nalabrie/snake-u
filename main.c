#include <stdint.h>
#include <stdbool.h>
#include <malloc.h>

#include <vpad/input.h>
#include <coreinit/screen.h>
#include <coreinit/cache.h>
#include <coreinit/time.h>
#include <whb/log_cafe.h>
#include <whb/log_udp.h>
#include <whb/log.h>
#include <whb/proc.h>

#define WHITE 0xffffff00
#define BLACK 0x00000000
#define RED 0xff000000

/* GLOBALS */

// enum of possible player movement directions
typedef enum direction {
    up, right, down, left, none
} direction;

// gamepad global variables
VPADStatus status;
VPADReadError error;
bool vpad_fatal = false;

// player global variables
direction snakeDirection = none;

/* FUNCTION PROTOTYPES */

// process user's button inputs each frame
static void handleGamepadInput();

// render to screen 'screenID'
static void renderToScreen(OSScreenID screenID, void *screenBuffer, size_t screenBufferSize);

// draws the world border around the screen 'screenID'
static void drawBorder(OSScreenID screenID);

/* MAIN */

int main(int argc, char **argv) {
    WHBProcInit();

    // error logging inits
    WHBLogCafeInit();
    WHBLogUdpInit();
    WHBLogPrint("Logging initialized");

    // init simple graphics API
    OSScreenInit();

    // get the size of each screen's buffer (tv and gamepad)
    size_t tvBufferSize = OSScreenGetBufferSizeEx(SCREEN_TV);
    size_t drcBufferSize = OSScreenGetBufferSizeEx(SCREEN_DRC);
    WHBLogPrintf("Will allocate 0x%X bytes for the TV, " \
                 "and 0x%X bytes for the DRC.",
                 tvBufferSize, drcBufferSize);

    // allocate memory area for screen buffers (MUST be 0x100 aligned)
    void *tvBuffer = memalign(0x100, tvBufferSize);
    void *drcBuffer = memalign(0x100, drcBufferSize);

    // check that allocation actually succeeded
    if (!tvBuffer || !drcBuffer) {
        WHBLogPrint("Out of memory (screen buffer allocation failed)");

        // free screen buffer memory
        if (tvBuffer) free(tvBuffer);
        if (drcBuffer) free(drcBuffer);

        // deinit everything
        WHBLogPrint("Quitting.");
        OSScreenShutdown();
        WHBProcShutdown();
        WHBLogCafeDeinit();
        WHBLogUdpDeinit();

        // return error code
        return 1;
    }

    // screen buffers are good, set them
    OSScreenSetBufferEx(SCREEN_TV, tvBuffer);
    OSScreenSetBufferEx(SCREEN_DRC, drcBuffer);

    // enable both screens
    OSScreenEnableEx(SCREEN_TV, true);
//    OSScreenEnableEx(SCREEN_DRC, true);

    // setup complete, enter main game loop
    while (WHBProcIsRunning()) {
        // get player input
        handleGamepadInput();

        // clear tv buffer, fill with black
        OSScreenClearBufferEx(SCREEN_TV, BLACK);

        // draw the border around the screen edges
        drawBorder(SCREEN_TV);

        // work completed, render to tv screen
        renderToScreen(SCREEN_TV, tvBuffer, tvBufferSize);
    }

    // if we get here, ProcUI said we should quit
    WHBLogPrint("Quitting.");

    // free the screen buffers
    if (tvBuffer) free(tvBuffer);
    if (drcBuffer) free(drcBuffer);

    // deinit everything
    OSScreenShutdown();
    WHBProcShutdown();
    WHBLogCafeDeinit();
    WHBLogUdpDeinit();

    return 0;
}

/* FUNCTION IMPLEMENTATIONS */

static void handleGamepadInput() {
    // read button input from gamepad
    VPADRead(VPAD_CHAN_0, &status, 1, &error);

    // check for gamepad errors
    switch (error) {
        case VPAD_READ_SUCCESS: {
            // read successful
            break;
        }
        case VPAD_READ_NO_SAMPLES: {
            // no data read on this frame
            break;
        }
        case VPAD_READ_INVALID_CONTROLLER: {
            // gamepad disconnected or 'invalid' in some other way
            WHBLogPrint("Gamepad disconnected!");
            vpad_fatal = true;
            break;
        }
        default: {
            // undocumented error, this should never happen
            WHBLogPrintf("Unknown VPAD error! %08X", error);
            vpad_fatal = true;
            break;
        }
    }

    // if there was a fatal error, stop here
    if (vpad_fatal) return;

    // read d-pad button presses and assign the corresponding player snake movement
    if (status.trigger & VPAD_BUTTON_UP) snakeDirection = up;
    else if (status.trigger & VPAD_BUTTON_RIGHT) snakeDirection = right;
    else if (status.trigger & VPAD_BUTTON_DOWN) snakeDirection = down;
    else if (status.trigger & VPAD_BUTTON_LEFT) snakeDirection = left;
}

static void renderToScreen(OSScreenID screenID, void *screenBuffer, size_t screenBufferSize) {
    // flush the screen buffer before rendering
    DCFlushRange(screenBuffer, screenBufferSize);

    // flip screen buffers (render to tv)
    OSScreenFlipBuffersEx(screenID);
}

static void drawBorder(OSScreenID screenID) {
    switch (screenID) {
        case SCREEN_TV:
            // draw white 20px border on tv edges
            for (int x = 0; x < 1280; ++x) {
                for (int y = 0; y < 20; ++y) {
                    OSScreenPutPixelEx(screenID, x, y, WHITE);  // top
                    OSScreenPutPixelEx(screenID, x, 700 + y, WHITE);  // bottom
                }
            }
            for (int x = 0; x < 20; ++x) {
                for (int y = 0; y < 720; ++y) {
                    OSScreenPutPixelEx(screenID, x, y, WHITE);  // left
                    OSScreenPutPixelEx(screenID, 1260 + x, y, WHITE);  // right
                }
            }
            break;
        case SCREEN_DRC:
            // unimplemented
            break;
        default:
            // should never occur, there are always two screens
            break;
    }
}