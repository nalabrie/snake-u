// standard C includes
#include <malloc.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

// Wii U includes
#include <coreinit/cache.h>
#include <coreinit/screen.h>
#include <coreinit/time.h>
#include <vpad/input.h>
#include <whb/log.h>
#include <whb/log_cafe.h>
#include <whb/log_udp.h>
#include <whb/proc.h>

#define WHITE 0xffffff00
#define GRAY 0x80808000
#define BLACK 0x00000000
#define RED 0xff000000
#define GREEN 0x00800000

#define BLOCK_SIZE 20

/* GLOBALS */

// enum of possible player movement directions
typedef enum direction { up, right, down, left, none } direction;

// gamepad global variables
VPADStatus status;
VPADReadError error;
bool vpad_fatal = false;

// scoring global variables
unsigned int score = 0;
unsigned int highScore = 0;  // TODO

// snake (player) struct
static struct snake {
  unsigned int x, y, length;
  direction direction;

  // parallel arrays to store the coordinates of the snake's body
  unsigned int body_x[(1240 / BLOCK_SIZE) * (680 / BLOCK_SIZE)];
  unsigned int body_y[(1240 / BLOCK_SIZE) * (680 / BLOCK_SIZE)];
} snake = {300, 340, 4, none};

// apple struct
static struct apple { unsigned int x, y; } apple = {980, 340};

// global variables for the screen buffers (tv and gamepad)
void *tvBuffer;
void *drcBuffer;

// frame-time constants
static const double FPS = 5.0;  // desired frames per second (only adjust this value)
static const double FRAME_TIME = 1000.0 / FPS;                   // frame-time in milliseconds
static const unsigned int FRAME_TIME_NS = FRAME_TIME * 1000000;  // frame-time in nanoseconds

/* FUNCTION PROTOTYPES */

// process user's button inputs
static void handleGamepadInput();

// render to screen 'screenID'
static void renderToScreen(OSScreenID screenID, void *screenBuffer, size_t screenBufferSize);

// draws the world border around the screen 'screenID'
static void drawBorder(OSScreenID screenID);

// draws a 'color' colored square of size 'BLOCK_SIZE' on screen 'screenID'
// (note: (x,y) starts at top-left corner)
static void drawSquare(OSScreenID screenID, uint32_t x_start, uint32_t y_start, uint32_t color);

// draw the snake to the screen 'screenID'
static void drawSnake(OSScreenID screenID);

// moves the snake in direction requested by the gamepad
static void moveSnake();

// de-initializes everything necessary for a clean shutdown of the game
static void shutdown();

// show debug messages
static void showDebug();

// checks if the snake has collided with either itself, the border, or the apple
// returns 'true' when snake died (hit itself or border)
// moves apple when collided
static bool checkSnakeCollision();

// prints the current score to the screen
static void showScore();

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
  WHBLogPrintf("Will allocate 0x%X bytes for the TV, and 0x%X bytes for the DRC.", tvBufferSize,
               drcBufferSize);

  // allocate memory area for screen buffers (MUST be 0x100 aligned)
  tvBuffer = memalign(0x100, tvBufferSize);
  drcBuffer = memalign(0x100, drcBufferSize);

  // check that allocation actually succeeded
  if (!tvBuffer || !drcBuffer) {
    WHBLogPrint("Out of memory (screen buffer allocation failed)");

    // close game and return error code
    shutdown();
    return 1;
  }

  // screen buffers are good, set them
  OSScreenSetBufferEx(SCREEN_TV, tvBuffer);
  OSScreenSetBufferEx(SCREEN_DRC, drcBuffer);

  // enable both screens
  OSScreenEnableEx(SCREEN_TV, true);
  // OSScreenEnableEx(SCREEN_DRC, true);

  // setup timer related variables
  double timeCounter = 0;  // how much time has passed between 'thisTime' and 'lastTime'
  OSTick thisTime = OSGetSystemTick();  // current system time
  OSTick lastTime = thisTime;           // system time from the last time 'thisTime' was updated

  // setup starting snake body locations
  for (unsigned int i = 0; i < snake.length - 1; i++) {
    snake.body_x[i] = snake.x - (i + 1) * BLOCK_SIZE;
    snake.body_y[i] = snake.y;
  }

  // represents if the game is over or not
  bool gameOver = false;

  // seed random number generator with system time (I think? no documentation)
  srand(OSGetTime());

  // setup complete, enter main game loop
  while (WHBProcIsRunning()) {
    // get player input
    handleGamepadInput();

    // do timer related calculations
    thisTime = OSGetSystemTick();
    timeCounter += (double)(thisTime - lastTime);
    lastTime = thisTime;
    if (timeCounter > OSNanosecondsToTicks(FRAME_TIME_NS)) {
      /* everything inside this 'if' runs every 'FRAME_TIME' milliseconds (every frame) */

      // reset the time counter for the next loop
      timeCounter -= OSNanosecondsToTicks(FRAME_TIME_NS);

      // clear tv buffer, fill with black
      OSScreenClearBufferEx(SCREEN_TV, BLACK);

      // draw the border around the screen edges
      drawBorder(SCREEN_TV);

      // move snake, check for a collision, draw the snake and apple
      moveSnake();
      gameOver = checkSnakeCollision();
      drawSnake(SCREEN_TV);
      drawSquare(SCREEN_TV, apple.x, apple.y, RED);  // apple

      showScore();
      // showDebug();

      // work completed, render to tv screen
      renderToScreen(SCREEN_TV, tvBuffer, tvBufferSize);
    }

    // end the game if the snake is dead
    if (gameOver) break;  // TODO: placeholder, just kills game
  }

  // if we get here, ProcUI said we should quit
  shutdown();

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
      // no data read on this frame, return now
      return;
    }
    case VPAD_READ_INVALID_CONTROLLER: {
      // gamepad disconnected or 'invalid' in some other way
      WHBLogPrint("Gamepad disconnected!");
      vpad_fatal = true;
      return;  // fatal error, stop here
    }
    default: {
      // undocumented error, this should never happen
      WHBLogPrintf("Unknown VPAD error! %08X", error);
      vpad_fatal = true;
      return;  // fatal error, stop here
    }
  }

  // read d-pad button presses and assign the corresponding player snake movement
  if (status.trigger & VPAD_BUTTON_UP)
    snake.direction = up;
  else if (status.trigger & VPAD_BUTTON_RIGHT)
    snake.direction = right;
  else if (status.trigger & VPAD_BUTTON_DOWN)
    snake.direction = down;
  else if (status.trigger & VPAD_BUTTON_LEFT)
    snake.direction = left;
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
      // draw 20px wide gray border on tv edges
      for (int x = 0; x < 1280; ++x) {
        for (int y = 0; y < 20; ++y) {
          OSScreenPutPixelEx(screenID, x, y, GRAY);        // top
          OSScreenPutPixelEx(screenID, x, 700 + y, GRAY);  // bottom
        }
      }
      for (int x = 0; x < 20; ++x) {
        for (int y = 0; y < 720; ++y) {
          OSScreenPutPixelEx(screenID, x, y, GRAY);         // left
          OSScreenPutPixelEx(screenID, 1260 + x, y, GRAY);  // right
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

static void drawSquare(OSScreenID screenID, uint32_t x_start, uint32_t y_start, uint32_t color) {
  for (int x = 0; x < BLOCK_SIZE; ++x) {
    for (int y = 0; y < BLOCK_SIZE; ++y) {
      OSScreenPutPixelEx(screenID, x_start + x, y_start + y, color);
    }
  }
}

static void drawSnake(OSScreenID screenID) {
  // draw snake head
  drawSquare(screenID, snake.x, snake.y, GREEN);

  // draw snake body
  for (unsigned int i = 0; i < snake.length - 1; i++) {
    drawSquare(screenID, snake.body_x[i], snake.body_y[i], GREEN);
  }
}

static void moveSnake() {
  // snake cannot move the reverse of the direction it is already moving
  static direction previousDirection = none;
  switch (previousDirection) {
    case up:
      if (snake.direction == down) snake.direction = previousDirection;
      break;
    case right:
      if (snake.direction == left) snake.direction = previousDirection;
      break;
    case down:
      if (snake.direction == up) snake.direction = previousDirection;
      break;
    case left:
      if (snake.direction == right) snake.direction = previousDirection;
      break;
    case none:
      if (snake.direction == left) snake.direction = previousDirection;
      break;
    default:
      // should never occur, log error
      WHBLogPrint("unknown error while moving snake");
      break;
  }

  // store old snake head position
  unsigned int temp_x = snake.x;
  unsigned int temp_y = snake.y;

  // move snake head
  switch (snake.direction) {
    case up:
      snake.y -= BLOCK_SIZE;
      break;
    case right:
      snake.x += BLOCK_SIZE;
      break;
    case down:
      snake.y += BLOCK_SIZE;
      break;
    case left:
      snake.x -= BLOCK_SIZE;
      break;
    case none:
      // do not move
      return;
  }

  // move snake body
  for (unsigned int i = snake.length - 2; i > 0; i--) {
    snake.body_x[i] = snake.body_x[i - 1];
    snake.body_y[i] = snake.body_y[i - 1];
  }
  snake.body_x[0] = temp_x;
  snake.body_y[0] = temp_y;

  // TODO: account for new body addition to fix spawning bug

  // store current direction as previous direction for next function call
  previousDirection = snake.direction;
}

static void shutdown() {
  // free screen buffer memory
  if (tvBuffer) free(tvBuffer);
  if (drcBuffer) free(drcBuffer);

  // de-init everything
  WHBLogPrint("Quitting.");
  OSScreenShutdown();
  WHBProcShutdown();
  WHBLogCafeDeinit();
  WHBLogUdpDeinit();
}

static void showDebug() {
  // snake movement debug messages
  switch (snake.direction) {
    case up:
      OSScreenPutFontEx(SCREEN_TV, 0, 2, "snake is moving up");
      break;
    case right:
      OSScreenPutFontEx(SCREEN_TV, 0, 2, "snake is moving right");
      break;
    case down:
      OSScreenPutFontEx(SCREEN_TV, 0, 2, "snake is moving down");
      break;
    case left:
      OSScreenPutFontEx(SCREEN_TV, 0, 2, "snake is moving left");
      break;
    case none:
      OSScreenPutFontEx(SCREEN_TV, 0, 2, "snake is not moving");
      break;
    default:
      OSScreenPutFontEx(SCREEN_TV, 0, 2, "unknown error");
      break;
  }

  // display frame counter
  static int frameCounter = 1;
  char buffer[256];
  itoa(frameCounter, buffer, 10);
  OSScreenPutFontEx(SCREEN_TV, 0, 3, buffer);
  frameCounter++;
}

static bool checkSnakeCollision() {
  // check for collision with itself
  for (unsigned int i = 0; i < snake.length - 1; i++) {
    if (snake.x == snake.body_x[i] && snake.y == snake.body_y[i]) return true;
  }

  // check for collision with the border
  if (snake.x < 20 || snake.x >= 1260) return true;
  if (snake.y < 20 || snake.y >= 700) return true;

  // check for collision with apple
  if (snake.x == apple.x && snake.y == apple.y) {
    // increment score and snake length
    score++;
    snake.length++;

    // move apple to new random location
    apple.x = (rand() % (1240 / BLOCK_SIZE)) * BLOCK_SIZE + 20;
    apple.y = (rand() % (680 / BLOCK_SIZE)) * BLOCK_SIZE + 20;
  }

  // snake is still alive
  return false;
}

static void showScore() {
  char buffer[16];
  sprintf(buffer, "score: %u", score);
  OSScreenPutFontEx(SCREEN_TV, 0, 1, buffer);
}
