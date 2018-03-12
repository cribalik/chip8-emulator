#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include <SDL2/SDL.h>

typedef unsigned short u16;
typedef unsigned char u8;
typedef unsigned int uint;
#define ARRAY_LEN(a) (sizeof(a)/sizeof(*a))


static u8 memory[4096];
static u8 V[16];
static u16 I; // index register
static u16 pc = 0x200; // program counter
static u8 screen_buffer[64*32];
static u8 delay_timer;
static u8 sound_timer;
static u16 stack[16];
static int sp;
static bool key[16];
static bool draw_flag;

static SDL_Keycode keys[] = {
  SDLK_x, // 0
  SDLK_1, // 1
  SDLK_2, // 2
  SDLK_3, // 3
  SDLK_q, // 4
  SDLK_w, // 5
  SDLK_e, // 6
  SDLK_a, // 7
  SDLK_s, // 8
  SDLK_d, // 9
  SDLK_z, // A
  SDLK_c, // B
  SDLK_4, // C
  SDLK_r, // D
  SDLK_f, // E
  SDLK_v, // F
};

static void draw(u8 X, u8 Y, u8 height) {
}

static void unsupported_opcode(u16 opcode) {
  printf("Unsupported opcode %i\n", opcode);
  exit(1);
}

// returns 0xFF if no key was pressed
static u8 handle_event(SDL_Event &event) {
  bool key_hit = false;
  switch (event.type) {
    case SDL_WINDOWEVENT:
      if (event.window.event == SDL_WINDOWEVENT_CLOSE)
        exit(0);
      break;

    case SDL_KEYDOWN:
      if (event.key.keysym.sym == SDLK_ESCAPE)
        exit(0);
      for (int i = 0; i < ARRAY_LEN(keys); ++i) {
        if (keys[i] == event.key.keysym.sym) {
          key[i] = true;
          return i;
        }
      }
      break;

    case SDL_KEYUP:
      for (int i = 0; i < ARRAY_LEN(keys); ++i) {
        if (keys[i] == event.key.keysym.sym) {
          key[i] = false;
          break;
        }
      }
      break;
  }
  return 0xFF;
}

static u8 get_key() {
  SDL_Event event;
  printf("waiting for key\n");
  while (1) {
    if (!SDL_WaitEvent(&event)) {
      printf("Error while waiting for keypress: %s\n", SDL_GetError());
      exit(1);
    }

    u8 k = handle_event(event);
    if (k != 0xFF) {
      key[k] = false;
      return k;
    }
  }
}

template<class T>
T max(T a, T b) {
  return a < b ? b : a;
}

template<class T>
T min(T a, T b) {
  return b < a ? b : a;
}

static int beep_counter;
static void audio_callback(void *userdata, Uint8 *_stream, int len) {
  static float alpha = 0.0f;

  float *stream = (float*) _stream;
  len = len/sizeof(float);

  int bp = beep_counter;

  if (!bp) {
    memset(stream, 0, len * sizeof(*stream));
    return;
  }

  const float amp = 0.3f;
  const float freq = 0.1f;
  int i;
  for (i = 0; i < min(len, bp); ++i) {
    stream[i] = sin(alpha) * amp;
    alpha += freq;
  }
  for (; i < len; ++i)
    stream[i] = 0.0f;

  bp -= len;
  if (bp < 0)
    bp = 0;
  beep_counter = bp;
}

int main(int argc, const char *argv[]) {
  srand(time(0));

  if (argc < 2) {
    printf("Usage: chip8 FILE\n");
    exit(1);
  }

  FILE *file = fopen(argv[1], "rb");
  if (!file) {
    printf("Failed to open file %s\n", argv[1]);
    exit(1);
  }

  int num_read = fread(memory+0x200, 1, sizeof(memory) - 0x200, file);
  if (!feof(file)) {
    printf("Faield to read entire file\n");
    exit(1);
  }
  printf("read program of size %i\n", num_read);

  /* Fix for some builds of SDL 2.0.4, see https://bugs.gentoo.org/show_bug.cgi?id=610326 */
  setenv("XMODIFIERS", "@im=none", 1);
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
    printf("Failed to init sdl: %s\n", SDL_GetError());
    exit(1);
  }

  SDL_Window *window = SDL_CreateWindow("chip8", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 64*12, 32*12, 0);
  if (!window) {
    printf("Failed to create window: %s\n", SDL_GetError());
    exit(1);
  }

  SDL_Surface *sdl_screen = SDL_GetWindowSurface(window);
  if (!sdl_screen) {
    printf("Failed to get window surface: %s\n", SDL_GetError());
    exit(1);
  }

  SDL_Surface *sdl_surface = SDL_CreateRGBSurface(0, 64, 32, 32, 0, 0, 0, 0);
  if (!sdl_surface) {
    printf("Failed to create rgb surface: %s\n", SDL_GetError());
    exit(1);
  }

  SDL_AudioSpec spec = {};
  spec.freq = 48000;
  spec.format = AUDIO_F32;
  spec.channels = 1;
  spec.samples = 1024;
  spec.callback = audio_callback;
  SDL_AudioDeviceID audio_id = SDL_OpenAudioDevice(0, 0, &spec, 0, SDL_AUDIO_ALLOW_ANY_CHANGE);
  if (!audio_id) {
    printf("Failed to get audio device id: %s\n", SDL_GetError());
    exit(1);
  }
  SDL_PauseAudioDevice(audio_id, 0);

  // init fontset
  {
    unsigned char chip8_fontset[80] = {
      0xF0, 0x90, 0x90, 0x90, 0xF0, // 0
      0x20, 0x60, 0x20, 0x20, 0x70, // 1
      0xF0, 0x10, 0xF0, 0x80, 0xF0, // 2
      0xF0, 0x10, 0xF0, 0x10, 0xF0, // 3
      0x90, 0x90, 0xF0, 0x10, 0x10, // 4
      0xF0, 0x80, 0xF0, 0x10, 0xF0, // 5
      0xF0, 0x80, 0xF0, 0x90, 0xF0, // 6
      0xF0, 0x10, 0x20, 0x40, 0x40, // 7
      0xF0, 0x90, 0xF0, 0x90, 0xF0, // 8
      0xF0, 0x90, 0xF0, 0x10, 0xF0, // 9
      0xF0, 0x90, 0xF0, 0x90, 0x90, // A
      0xE0, 0x90, 0xE0, 0x90, 0xE0, // B
      0xF0, 0x80, 0x80, 0x80, 0xF0, // C
      0xE0, 0x90, 0x90, 0x90, 0xE0, // D
      0xF0, 0x80, 0xF0, 0x80, 0xF0, // E
      0xF0, 0x80, 0xF0, 0x80, 0x80  // F
    };
    for (int i = 0; i < 80; ++i)
      memory[i] = chip8_fontset[i];
  }

  for (int iter = 0;; ++iter) {
    // printf("iter %i\n", iter);

    SDL_Delay(3);

    for (SDL_Event event; SDL_PollEvent(&event);) {
      handle_event(event);
    }

    const uint opcode = memory[pc] << 8 | memory[pc+1];
    pc += 2;
    switch (opcode & 0xF000) {
      case 0x0000:
        switch (opcode) {
          case 0x00E0: // clear screen_buffer
            memset(screen_buffer, 0, sizeof(screen_buffer));
            draw_flag = true;
            break;

          case 0x00EE: // return
            pc = stack[--sp];
            break;

          default:
            unsupported_opcode(opcode);
            break;
        };
        break;

      case 0x1000: // jump
        pc = opcode & 0xFFF;
        break;

      case 0x2000: // call
        stack[sp++] = pc;
        pc = opcode & 0xFFF;
        break;

      case 0x3000: // Vx == NN
        if (V[(opcode & 0xF00) >> 8] == (opcode & 0xFF))
          pc += 2;
        break;

      case 0x4000: // Vx != NN
        if (V[(opcode & 0xF00) >> 8] != (opcode & 0xFF))
          pc += 2;
        break;

      case 0x5000: // Vx == Vy
        assert((opcode & 0xF) == 0);
        if (V[(opcode & 0xF00) >> 8] == V[(opcode & 0xF0) >> 4])
          pc += 2;
        break;

      case 0x6000: // Vx = NN
        V[(opcode & 0xF00) >> 8] = (opcode & 0xFF);
        break;

      case 0x7000: // Vx += NN (carry flag is not changed)
        V[(opcode & 0xF00) >> 8] += (opcode & 0xFF);
        break;

      case 0x8000: { // maths
        u8 &Vx = V[(opcode & 0x0F00) >> 8];
        u8 &Vy = V[(opcode & 0x00F0) >> 4];
        switch (opcode & 0xF) {
          case 0: // Vx = Vy
            Vx = Vy;
            break;

          case 1: // Vx = Vx|Vy
            Vx = Vx | Vy;
            break;

          case 2: // Vx = Vx&Vy
            Vx = Vx & Vy;
            break;

          case 3: // Vx = Vx^Vy
            Vx = Vx ^ Vy;
            break;

          case 4: // Vx += Vy
            V[0xF] = Vy > 0xFF - Vx;
            Vx = (Vx + Vy) & 0xFF;
            break;

          case 5: // Vx -= Vy
            V[0xF] = !(Vx < Vy);
            Vx = (Vx - Vy) & 0xFF;
            break;

          case 6: // Vx = Vy = Vy >> 1
            V[0xF] = Vy & 0x1;
            Vx = Vy = Vy >> 1;
            break;

          case 7: // Vx = Vy - Vx;
            V[0xF] = !(Vy < Vx);
            Vx = (Vy - Vx) & 0xFF;
            break;

          case 0xE: // Vx = Vy = Vy << 1
            V[0xF] = Vy & 0x8;
            Vx = Vy = Vy << 1;
            break;

          default:
            printf("Unsupported opcode %i\n", opcode);
            exit(1);
        }
      } break;

      case 0x9000: // Vx != Vy
        assert((opcode & 0xF) == 0);
        if (V[(opcode & 0xF00) >> 8] != V[(opcode & 0xF0) >> 4])
          pc += 2;
        break;

      case 0xA000:
        I = opcode & 0xFFF;
        break;

      case 0xB000:
        pc = V[0] + (opcode & 0xFFF);
        break;

      case 0xC000: // Vx = rand()&NN
        V[(opcode & 0xF00) >> 8] = rand() & (opcode & 0xFF);
        break;

      case 0xD000: {// draw(Vx,Vy,N)
        u8 X = V[(opcode & 0xF00) >> 8];
        u8 Y = V[(opcode & 0xF0) >> 4];
        u8 height = opcode & 0xF;

        V[0xF] = 0;
        for (int yline = 0; yline < height; yline++) {
          u8 pixel = memory[I + yline];
          for (int xline = 0; xline < 8; xline++) {
            if ((pixel & (0x80 >> xline)) != 0)
            {
              // if (X + xline >= 64 || Y + yline >= 32)
                // continue;
              int idx = ((Y + yline)&31)*64 + ((X + xline)&63);
              if (screen_buffer[idx] == 1)
                V[0xF] = 1;                                 
              screen_buffer[idx] ^= 0x1;
            }
          }
        }

        draw_flag = true;
      } break;

      case 0xE000:
        switch (opcode & 0xFF) {
          case 0x9E: // key[Vx]
            if (key[V[(opcode & 0xF00) >> 8]])
              pc += 2;
            break;

          case 0xA1: // !key[Vx]
            if (!key[V[(opcode & 0xF00) >> 8]])
              pc += 2;
            break;

          default:
            unsupported_opcode(opcode);
            break;
        }
        break;

      case 0xF000: {
        u8 &Vx = V[(opcode & 0xF00) >> 8];
        switch (opcode & 0xFF) {
          case 0x07: // Vx = get_delay()  Sets VX to the value of the delay timer.
            Vx = delay_timer;
            break;

          case 0x0A: // Vx = get_key()  A key press is awaited, and then stored in VX. (Blocking Operation. All instruction halted until next key event)
            Vx = get_key();
            break;

          case 0x15: // delay_timer(Vx) Sets the delay timer to VX.
            delay_timer = Vx;
            break;

          case 0x18: // sound_timer(Vx) Sets the sound timer to VX.
            sound_timer = Vx;
            break;

          case 0x1E: // I +=Vx  Adds VX to I.[3]
            V[0xF] = Vx > (0xFFFF - I);
            I = (I + Vx) & 0xFFFF;
            break;

          case 0x29: // I=sprite_addr[Vx] Sets I to the location of the sprite for the character in VX. Characters 0-F (in hexadecimal) are represented by a 4x5 font.
            I = V[(opcode & 0xF00) >> 8]*5;
            break;

          case 0x33: { // set_BCD(Vx);
            memory[I] = Vx / 100;
            memory[I+1] = (Vx / 10) % 10;
            memory[I+2] = Vx % 10;
          } break;

          case 0x55: // reg_dump(Vx,&I) Stores V0 to VX (including VX) in memory starting at address I. I is increased by 1 for each value written.
            for (int i = 0; i <= ((opcode & 0xF00) >> 8); ++i)
              memory[I++] = V[i];
            break;

          case 0x65: // reg_load(Vx,&I) Fills V0 to VX (including VX) with values from memory starting at address I. I is increased by 1 for each value written.
            for (int i = 0; i <= ((opcode & 0xF00) >> 8); ++i)
              V[i] = memory[I++];
            break;

          default:
            unsupported_opcode(opcode);
            break;
        }
      } break;

      default:
        unsupported_opcode(opcode);
        break;
    }

    if (delay_timer)
      --delay_timer;
    if (sound_timer) {
      --sound_timer;
      beep_counter = 3000;
      // if (!sound_timer)
      //   printf("BEEP\n");
    }

    if (draw_flag) {
      SDL_FillRect(sdl_surface, 0, SDL_MapRGB(sdl_surface->format, 255, 0, 0));
      for (int x = 0; x < 64; ++x)
      for (int y = 0; y < 32; ++y) {
        Uint32 color = screen_buffer[y*64 + x] ? SDL_MapRGB(sdl_surface->format, 255, 255, 255) : SDL_MapRGB(sdl_surface->format, 0, 0, 0);
        SDL_Rect r = {x, y, 1, 1};
        SDL_FillRect(sdl_surface, &r, color);
      }
      SDL_BlitScaled(sdl_surface, 0, sdl_screen, 0);
      SDL_UpdateWindowSurface(window);
      draw_flag = false;
    }

  }
}

