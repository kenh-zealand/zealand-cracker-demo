/*
 * ZEALAND BYTE DIVISION — Native C Demo
 * Synthwave cracker intro with ProTracker MOD music
 * Requires: SDL2
 * Build:  see Makefile
 */

#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#include "font.h"
#include "mod_player.h"

// ── Config ───────────────────────────────────────────────────────
static int win_w = 1024;
static int win_h = 600;
#define TARGET_FPS     60
#define AUDIO_BUF     512   // frames per audio callback

// ── MOD file list ────────────────────────────────────────────────
static const char *MOD_FILES[] = {
    "../mods/hardwired-intro.mod",
    "../mods/in-the-kitchen.mod",
    "../mods/introfronty.mod",
    "../mods/monday.mod",
    "../mods/space-debris.mod",
};
static const char *MOD_LABELS[] = {
    "HARDWIRED", "IN THE KITCHEN", "INTRO FRONTY", "MONDAY", "SPACE DEBRIS"
};
#define NUM_MODS 5

// ── Globals ──────────────────────────────────────────────────────
static SDL_Window   *window   = NULL;
static SDL_Renderer *renderer = NULL;
static SDL_Texture  *screen   = NULL;   // software render target
static uint32_t     *pixels   = NULL;
static int           pitch_px = 1024;

static ModPlayer  player;
static ModFile   *current_mod = NULL;
static int        mod_idx     = 0;
static SDL_mutex *audio_mutex = NULL;

// Audio ring buffer placeholder (unused — audio mixed directly in callback)
#define RING_SIZE 1

// ── Stars ────────────────────────────────────────────────────────
#define NUM_STARS 200
typedef struct { float x, y, speed, alpha, phase; int size; } Star;
static Star stars[NUM_STARS];

// ── Scroller ─────────────────────────────────────────────────────
static const char *SCROLL_TEXT =
    "  * ZEALAND BYTE DIVISION PRESENTS THE ULTIMATE CRACKER INTRO *  "
    "  THIS PRODUCTION WAS HAND-CODED IN PURE C WITH SDL2 AND ZERO BRAIN DAMAGE!  "
    "  GREETS FLY OUT TO: RAZOR 1911  FAIRLIGHT  FUTURE CREW  SKIDROW  PARADOX  "
    "CLASS  TRITON  QUARTEX  ECLIPSE  PRESTIGE  AND ALL OTHER TRUE SCENERS!  "
    "  KEYS: [N] NEXT MOD  [P] PREV MOD  [M] MUTE  [ESC] QUIT  "
    "  LONG LIVE THE AMIGA!  KEEP THE SCENE ALIVE!  100% C POWER!  * ";
static float scroll_x    = 1024.0f;
static float scroll_speed = 120.0f;  // pixels/sec

static float beat_visual  = 0.0f;   // smoothed beat for visuals
static int   muted        = 0;
static float master_vol   = 0.7f;

// Click region of the mod-name button (updated each frame by draw_topbar)
static SDL_Rect mod_btn_rect = {0, 0, 0, 0};

// ── Colour helpers ────────────────────────────────────────────────
static inline uint32_t rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    return ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}
static inline uint32_t blend(uint32_t dst, uint32_t src) {
    uint32_t sa = (src >> 24) & 0xFF;
    if (sa == 0)   return dst;
    if (sa == 255) return src;
    uint32_t da = 255 - sa;
    uint8_t r = (uint8_t)(((src>>16&0xFF)*sa + (dst>>16&0xFF)*da) >> 8);
    uint8_t g = (uint8_t)(((src>> 8&0xFF)*sa + (dst>> 8&0xFF)*da) >> 8);
    uint8_t b = (uint8_t)(((src    &0xFF)*sa + (dst    &0xFF)*da) >> 8);
    return rgba(r, g, b, 255);
}

// ── Pixel drawing ─────────────────────────────────────────────────
static inline void put_pixel(int x, int y, uint32_t c) {
    if (x < 0 || x >= win_w || y < 0 || y >= win_h) return;
    pixels[y * pitch_px + x] = blend(pixels[y * pitch_px + x], c);
}

static void draw_line(int x0, int y0, int x1, int y1, uint32_t c) {
    int dx = abs(x1-x0), sx = x0<x1?1:-1;
    int dy = -abs(y1-y0), sy = y0<y1?1:-1;
    int err = dx+dy;
    while (1) {
        put_pixel(x0, y0, c);
        if (x0==x1 && y0==y1) break;
        int e2 = 2*err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

static void fill_rect(int x, int y, int w, int h, uint32_t c) {
    for (int j = y; j < y+h; j++)
        for (int i = x; i < x+w; i++)
            put_pixel(i, j, c);
}

static void draw_hline(int y, uint32_t c) {
    if (y < 0 || y >= win_h) return;
    uint32_t *row = pixels + y * pitch_px;
    for (int x = 0; x < win_w; x++) row[x] = blend(row[x], c);
}

// ── Font rendering ────────────────────────────────────────────────
static void draw_text(const char *text, int x, int y, int scale, uint32_t c) {
    int cx = x;
    for (const char *p = text; *p; p++) {
        font_draw_char(pixels, pitch_px, win_h, cx, y, *p, scale, c);
        cx += 6 * scale;
    }
}

static int text_width(const char *text, int scale) {
    return (int)strlen(text) * 6 * scale;
}

// ── Draw with alpha-blended glow passes ───────────────────────────
// Fix: proper glow without void-expression trick
static void draw_glow_text(const char *text, int x, int y, int scale,
                           uint32_t core_color, uint32_t glow_color, int radius) {
    for (int dy = -radius; dy <= radius; dy++) {
        for (int dx = -radius; dx <= radius; dx++) {
            if (dx == 0 && dy == 0) continue;
            int cx2 = x;
            for (const char *p = text; *p; p++) {
                font_draw_char(pixels, pitch_px, win_h, cx2+dx, y+dy, *p, scale, glow_color);
                cx2 += 6 * scale;
            }
        }
    }
    draw_text(text, x, y, scale, core_color);
}

// ── Stars ─────────────────────────────────────────────────────────
static void init_stars(void) {
    srand((unsigned)time(NULL));
    for (int i = 0; i < NUM_STARS; i++) {
        stars[i].x     = rand() % win_w;
        stars[i].y     = rand() % (int)(win_h * 0.52f);
        stars[i].speed = 0.4f + (rand() % 100) / 40.0f;
        stars[i].alpha = 0.3f + (rand() % 70) / 100.0f;
        stars[i].phase = (rand() % 628) / 100.0f;
        stars[i].size  = (rand() % 5 == 0) ? 2 : 1;
    }
}

static void draw_stars(float t) {
    for (int i = 0; i < NUM_STARS; i++) {
        float blink = 0.35f + 0.65f * fabsf(sinf(stars[i].phase + t * stars[i].speed));
        uint8_t a = (uint8_t)(stars[i].alpha * blink * 255.0f);
        uint32_t c = rgba(200, 220, 255, a);
        int sx = (int)stars[i].x, sy = (int)stars[i].y;
        put_pixel(sx, sy, c);
        if (stars[i].size > 1) put_pixel(sx+1, sy, c);
    }
}

// ── Synthwave grid ────────────────────────────────────────────────
static void draw_grid(float t, float beat) {
    int gy  = (int)(win_h * 0.52f);
    int gh  = win_h - gy;

    // Background gradient (simple vertical fill)
    for (int y = gy; y < win_h; y++) {
        float frac = (float)(y - gy) / gh;
        uint8_t r = (uint8_t)(7  + (uint8_t)(frac * 3));
        uint8_t b = (uint8_t)(37 - (uint8_t)(frac * 12));
        uint32_t c = rgba(r, 0, b, 255);
        uint32_t *row = pixels + y * pitch_px;
        for (int x = 0; x < win_w; x++) row[x] = c;
    }

    // Neon stripes above the grid
    float bs = 1.0f + beat * 1.4f;
    struct { int y, h; uint8_t r, g, b; float a; } stripes[4] = {
        {gy-2,  2, 0,   (uint8_t)(80*bs),  (uint8_t)(180*bs), 0.9f},
        {gy-6,  3, 0,   (uint8_t)(50*bs),  (uint8_t)(140*bs), 0.8f},
        {gy-11, 4, (uint8_t)(30*bs),  0, (uint8_t)(110*bs), 0.85f},
        {gy-17, 5, (uint8_t)(100*bs), 0, (uint8_t)(160*bs), 0.7f},
    };
    for (int s = 0; s < 4; s++) {
        uint32_t c = rgba(stripes[s].r, stripes[s].g, stripes[s].b, (uint8_t)(stripes[s].a*255));
        fill_rect(0, stripes[s].y, win_w, stripes[s].h, c);
    }

    // Horizon glow
    float hg   = 18.0f + beat * 40.0f;
    float halpha = 0.55f + beat * 0.7f; if (halpha > 1.0f) halpha = 1.0f;
    for (int y = (int)(gy - hg); y <= (int)(gy + hg); y++) {
        float d = 1.0f - fabsf((float)(y - gy) / hg);
        uint8_t a = (uint8_t)(d * d * halpha * 255.0f);
        draw_hline(y, rgba(0, 200, 255, a));
    }

    // Beat flash
    if (beat > 0.35f) {
        float flash = (beat - 0.35f) / 0.65f;
        uint8_t fa = (uint8_t)(flash * 0.35f * 255.0f);
        for (int y = gy-4; y <= gy+4; y++) draw_hline(y, rgba(180, 240, 255, fa));
    }

    // Moving horizontal grid lines
    float speed = fmodf(t * 0.028f, 1.0f);
    for (int i = 0; i <= 22; i++) {
        float frac = fmodf((float)i / 22.0f + speed, 1.0f);
        int   ly   = gy + (int)(powf(frac, 1.65f) * gh);
        if (ly < gy || ly >= win_h) continue;
        float a = (0.08f + 0.72f * frac) * (1.0f + beat * 1.6f);
        if (a > 1.0f) a = 1.0f;
        uint8_t rv  = (uint8_t)fminf(255, (40  + 180*frac) * (1+beat*0.5f));
        uint8_t bv  = (uint8_t)fminf(255, (210 -  90*frac) * (1+beat*0.4f));
        draw_hline(ly, rgba(rv, 0, bv, (uint8_t)(a*255)));
    }

    // Perspective vertical lines
    float vpx = win_w * 0.5f;
    for (int i = 0; i <= 20; i++) {
        float frac = (float)i / 20.0f;
        float bx   = frac * win_w;
        float tx   = vpx + (bx - vpx) * 0.02f;
        float base = 0.08f + 0.32f * (1.0f - fabsf(frac - 0.5f) * 2.0f + 0.5f);
        float a    = base * (1.0f + beat * 2.2f); if (a > 1.0f) a = 1.0f;
        uint8_t pink = (uint8_t)fminf(255, 210 + beat * 45);
        uint8_t gmid = (uint8_t)fminf(255, 40  + beat * 60);
        draw_line((int)tx, gy, (int)bx, win_h-1, rgba(pink, gmid, 180, (uint8_t)(a*255)));
    }
}

// ── Logo ──────────────────────────────────────────────────────────
static void draw_logo(float t) {
    const char *LINE1 = "ZEALAND";
    const char *LINE2 = "BYTE DIV";

    int max_chars = 8; // BYTE DIV
    int sc = win_w * 82 / 100 / (max_chars * 6);
    int sc2 = (int)(win_h * 0.40f) / (7 * 2 + 3);
    if (sc > sc2) sc = sc2;
    if (sc < 3) sc = 3;

    int ch = 7 * sc;
    int gap = sc * 2;
    int totalH = ch * 2 + gap;

    int w1 = text_width(LINE1, sc);
    int w2 = text_width(LINE2, sc);
    int x1 = (win_w - w1) / 2;
    int x2 = (win_w - w2) / 2;
    float bob = sinf(t * 0.75f) * 4.0f;
    int y1 = (int)((win_h * 0.52f - totalH) / 2.0f + bob);
    int y2 = y1 + ch + gap;

    // Glow passes
    draw_glow_text(LINE1, x1, y1, sc, rgba(255,128,200,255), rgba(220,20,100,60),  6);
    draw_glow_text(LINE1, x1, y1, sc, rgba(255,128,200,255), rgba(240,40,120,100), 3);
    draw_glow_text(LINE2, x2, y2, sc, rgba(255,128,200,255), rgba(220,20,100,60),  6);
    draw_glow_text(LINE2, x2, y2, sc, rgba(255,128,200,255), rgba(240,40,120,100), 3);
}

// ── Top bar ───────────────────────────────────────────────────────
#define TOPBAR_H 36
static void draw_topbar(const char *mod_label, int midx, int total) {
    // Background
    fill_rect(0, 0, win_w, TOPBAR_H, rgba(3, 0, 14, 255));
    // Bottom border
    fill_rect(0, TOPBAR_H-2, win_w, 2, rgba(0, 136, 187, 255));

    // Group name
    draw_text("ZEALAND  BYTE  DIVISION", 12, 10, 2, rgba(0, 170, 221, 255));

    // Mod label box
    char label[64];
    snprintf(label, sizeof(label), "# %s  [%d/%d]", mod_label, midx+1, total);
    int lw = text_width(label, 1) + 16;
    int lx = win_w - lw - 12;
    // Store clickable rect for mouse hit-test
    mod_btn_rect.x = lx - 1; mod_btn_rect.y = 6;
    mod_btn_rect.w = lw + 2; mod_btn_rect.h = TOPBAR_H - 14;
    fill_rect(lx-1, 6, lw+2, TOPBAR_H-14, rgba(255,200,0,30));
    // Brighten box if mouse is hovering over it
    int mx, my; SDL_GetMouseState(&mx, &my);
    SDL_Point mpt = {mx, my};
    if (SDL_PointInRect(&mpt, &mod_btn_rect))
        fill_rect(lx-1, 6, lw+2, TOPBAR_H-14, rgba(255,200,0,40));
    fill_rect(lx-1, 6, lw+2, 1,            rgba(255,200,0,200));
    fill_rect(lx-1, TOPBAR_H-9, lw+2, 1,   rgba(255,200,0,200));
    fill_rect(lx-1, 6, 1, TOPBAR_H-14,     rgba(255,200,0,200));
    fill_rect(lx+lw, 6, 1, TOPBAR_H-14,    rgba(255,200,0,200));
    draw_text(label, lx+8, 11, 1, rgba(255,200,0,255));
}

// ── Scroller bar ──────────────────────────────────────────────────
// (win_h - SCROLL_H) computed inline as (win_h - SCROLL_H)
#define SCROLL_H   56
static void draw_scroller(float dt) {
    int sc    = 4;
    int char_w = 6 * sc;
    int char_h = 7 * sc;
    int base_y = (win_h - SCROLL_H) + (SCROLL_H - char_h) / 2;

    scroll_x -= scroll_speed * dt;
    int total_w = (int)(strlen(SCROLL_TEXT) * char_w);
    if (scroll_x < -(float)total_w) scroll_x += (float)total_w;

    // Background
    fill_rect(0, (win_h - SCROLL_H), win_w, SCROLL_H, rgba(2, 0, 16, 255));
    // Border top
    fill_rect(0, (win_h - SCROLL_H), win_w, 2, rgba(0, 136, 187, 255));

    // Draw characters
    for (int rep = 0; rep < 3; rep++) {
        float base_x = scroll_x + rep * total_w;
        if (base_x > win_w + char_w) continue;
        if (base_x + total_w < -char_w) continue;

        int len = (int)strlen(SCROLL_TEXT);
        for (int i = 0; i < len; i++) {
            float cx = base_x + i * char_w;
            if (cx + char_w < 0 || cx > win_w) continue;

            float wave  = sinf(cx * 0.014f + SDL_GetTicks() * 0.0026f) * (SCROLL_H * 0.30f);
            float phase = sinf(cx * 0.009f + SDL_GetTicks() * 0.0018f) * 0.5f + 0.5f;
            uint8_t r = (uint8_t)(210 + 45 * phase);
            uint8_t g = (uint8_t)(140 + 60 * phase);
            font_draw_char(pixels, pitch_px, win_h, (int)cx, (int)(base_y + wave),
                           SCROLL_TEXT[i], sc, rgba(r, g, 0, 255));
        }
    }

    // Edge fade (darken leftmost and rightmost columns)
    for (int x = 0; x < 80; x++) {
        uint8_t a = (uint8_t)(255 - x * 3);
        for (int y = (win_h - SCROLL_H); y < win_h; y++) {
            uint32_t c = rgba(2,0,16,a);
            pixels[y*pitch_px + x]           = blend(pixels[y*pitch_px + x], c);
            pixels[y*pitch_px + (win_w-1-x)] = blend(pixels[y*pitch_px + (win_w-1-x)], c);
        }
    }
}

// ── Bottom panels ─────────────────────────────────────────────────
#define PANEL_Y  (TOPBAR_H + (int)((win_h - TOPBAR_H - SCROLL_H) * 52 / 100) + (int)((win_h - TOPBAR_H - SCROLL_H) * 48 / 100))
static void draw_panels(void) {
    int py = win_h - SCROLL_H - 100;
    int ph = 96;
    int pw = (win_w - 12) / 2;

    for (int p = 0; p < 2; p++) {
        int px = 4 + p * (pw + 4);
        fill_rect(px, py, pw, ph, rgba(3, 0, 16, 255));
        fill_rect(px, py, pw, 1, rgba(0,136,187,200));
        fill_rect(px, py+ph-1, pw, 1, rgba(0,136,187,200));
        fill_rect(px, py, 1, ph, rgba(0,136,187,200));
        fill_rect(px+pw-1, py, 1, ph, rgba(0,136,187,200));
    }

    draw_text("ELITE STATUS REPORT", 12, py+6,  1, rgba(0,170,221,255));
    draw_text("GREETS FLY TO:",      pw+12, py+6, 1, rgba(0,170,221,255));

    draw_text("THIS RELEASE CRACKED", 12,    py+20, 1, rgba(0,255,136,255));
    draw_text("BY ZEALAND BYTE DIV!", 12,    py+32, 1, rgba(0,255,136,255));
    draw_text("100% WORKING",         12,    py+44, 1, rgba(0,255,136,255));

    draw_text("RAZOR 1911 FAIRLIGHT", pw+12, py+20, 1, rgba(0,255,136,255));
    draw_text("FUTURE CREW  SKIDROW", pw+12, py+32, 1, rgba(0,255,136,255));
    draw_text("PARADOX  QUARTEX",     pw+12, py+44, 1, rgba(0,255,136,255));
}

// ── Audio callback ────────────────────────────────────────────────
static void audio_callback(void *userdata, uint8_t *stream, int len) {
    (void)userdata;
    int16_t *out    = (int16_t*)stream;
    int      frames = len / 4; // stereo int16

    SDL_LockMutex(audio_mutex);
    if (current_mod && player.playing && !muted) {
        mod_mix(&player, out, frames);
        // Apply master volume
        for (int i = 0; i < frames*2; i++)
            out[i] = (int16_t)(out[i] * master_vol);
    } else {
        memset(stream, 0, len);
    }
    SDL_UnlockMutex(audio_mutex);
}

// ── Resize handler ────────────────────────────────────────────────
static void handle_resize(int w, int h) {
    win_w    = w;
    win_h    = h;
    pitch_px = w;

    free(pixels);
    pixels = (uint32_t*)malloc(w * h * sizeof(uint32_t));

    SDL_DestroyTexture(screen);
    screen = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                               SDL_TEXTUREACCESS_STREAMING, w, h);

    // Regenerate stars for new dimensions
    init_stars();
    // Keep scroll position valid
    if (scroll_x > (float)w) scroll_x = (float)w;
}
static void load_mod(int idx) {
    SDL_LockMutex(audio_mutex);
    mod_free(current_mod);
    current_mod = mod_load(MOD_FILES[idx]);
    if (current_mod) mod_player_init(&player, current_mod);
    SDL_UnlockMutex(audio_mutex);
    mod_idx = idx;
}

// ── Main ──────────────────────────────────────────────────────────
int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    SDL_SetMainReady();

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    window = SDL_CreateWindow(
        "ZEALAND BYTE DIVISION",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        win_w, win_h, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!window) { fprintf(stderr, "Window: %s\n", SDL_GetError()); return 1; }

    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) { fprintf(stderr, "Renderer: %s\n", SDL_GetError()); return 1; }

    screen = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                               SDL_TEXTUREACCESS_STREAMING, win_w, win_h);
    if (!screen) { fprintf(stderr, "Texture: %s\n", SDL_GetError()); return 1; }

    pixels = (uint32_t*)malloc(win_w * win_h * sizeof(uint32_t));
    if (!pixels) { fprintf(stderr, "OOM\n"); return 1; }

    audio_mutex = SDL_CreateMutex();
    init_stars();

    // Open audio
    SDL_AudioSpec want = {0}, got;
    want.freq     = MOD_SAMPLE_RATE;
    want.format   = AUDIO_S16SYS;
    want.channels = 2;
    want.samples  = AUDIO_BUF;
    want.callback = audio_callback;

    SDL_AudioDeviceID audio_dev = SDL_OpenAudioDevice(NULL, 0, &want, &got, 0);
    if (audio_dev == 0) {
        fprintf(stderr, "Audio: %s\n", SDL_GetError());
    } else {
        load_mod(0);
        SDL_PauseAudioDevice(audio_dev, 0);
    }

    uint32_t last_ticks = SDL_GetTicks();
    int running = 1;

    while (running) {
        uint32_t now = SDL_GetTicks();
        float dt = (now - last_ticks) / 1000.0f;
        if (dt > 0.05f) dt = 0.05f;
        last_ticks = now;
        float t_sec = now / 1000.0f;

        // ── Events ────────────────────────────────────────────────
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = 0;
            if (e.type == SDL_WINDOWEVENT &&
                e.window.event == SDL_WINDOWEVENT_RESIZED)
                handle_resize(e.window.data1, e.window.data2);
            if (e.type == SDL_KEYDOWN) {
                switch (e.key.keysym.sym) {
                    case SDLK_ESCAPE: running = 0; break;
                    case SDLK_n: load_mod((mod_idx + 1) % NUM_MODS); break;
                    case SDLK_p: load_mod((mod_idx - 1 + NUM_MODS) % NUM_MODS); break;
                    case SDLK_m:
                        muted = !muted;
                        break;
                    case SDLK_f:
                        SDL_SetWindowFullscreen(window,
                            SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN_DESKTOP
                            ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
                        break;
                }
            }
            if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                SDL_Point pt = { e.button.x, e.button.y };
                if (SDL_PointInRect(&pt, &mod_btn_rect))
                    load_mod((mod_idx + 1) % NUM_MODS);
            }
        }

        // ── Beat ──────────────────────────────────────────────────
        float raw_beat = 0.0f;
        SDL_LockMutex(audio_mutex);
        raw_beat = player.beat_energy;
        SDL_UnlockMutex(audio_mutex);
        // Fast attack, slow decay
        if (raw_beat > beat_visual) beat_visual = raw_beat;
        else                        beat_visual *= 0.88f;
        float beat = beat_visual * 2.5f; // scale up for visual impact
        if (beat > 1.0f) beat = 1.0f;

        // ── Clear ─────────────────────────────────────────────────
        memset(pixels, 0x05, win_w * win_h * sizeof(uint32_t));
        // Set proper background colour
        uint32_t bg = rgba(5, 0, 16, 255);
        for (int i = 0; i < win_w * win_h; i++) pixels[i] = bg;

        // ── Draw ──────────────────────────────────────────────────
        draw_stars(t_sec);
        draw_logo(t_sec);
        draw_grid(t_sec, beat);
        draw_topbar(MOD_LABELS[mod_idx], mod_idx, NUM_MODS);
        draw_panels();
        draw_scroller(dt);

        // ── Present ───────────────────────────────────────────────
        SDL_UpdateTexture(screen, NULL, pixels, win_w * sizeof(uint32_t));
        SDL_RenderCopy(renderer, screen, NULL, NULL);
        SDL_RenderPresent(renderer);
    }

    // Cleanup
    SDL_CloseAudioDevice(audio_dev);
    mod_free(current_mod);
    SDL_DestroyMutex(audio_mutex);
    free(pixels);
    SDL_DestroyTexture(screen);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
