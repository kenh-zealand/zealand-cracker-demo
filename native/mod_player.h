#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// ── ProTracker MOD constants ────────────────────────────────────
#define MOD_CHANNELS    4
#define MOD_MAX_INSTR   31
#define MOD_ROWS        64
#define MOD_SAMPLE_RATE 44100

// Amiga period table (4 octaves × 12 semitones, finetune 0)
static const int MOD_PERIODS[48] = {
    1712,1616,1524,1440,1356,1280,1208,1140,1076,1016, 960, 906,
     856, 808, 762, 720, 678, 640, 604, 570, 538, 508, 480, 453,
     428, 404, 381, 360, 339, 320, 302, 285, 269, 254, 240, 226,
     214, 202, 190, 180, 170, 160, 151, 143, 135, 127, 120, 113,
};

typedef struct {
    char     name[23];
    int      length;       // in samples (bytes)
    int      finetune;     // -8..7
    int      volume;       // 0..64
    int      repeat_start;
    int      repeat_len;
    int8_t  *data;
} ModInstrument;

typedef struct {
    int sample;   // 1-based
    int period;
    int effect;   // top nibble = cmd, low byte = param
} ModNote;

typedef struct {
    ModNote rows[MOD_ROWS][MOD_CHANNELS];
} ModPattern;

typedef struct {
    char         title[21];
    ModInstrument instr[MOD_MAX_INSTR];
    int          song_length;
    uint8_t      order[128];
    int          num_patterns;
    ModPattern  *patterns;
} ModFile;

// ── Channel state ───────────────────────────────────────────────
typedef struct {
    int      sample;        // 1-based, 0 = none
    int      period;
    int      volume;        // 0..64
    int      finetune;
    // playback position as fixed-point (16.16)
    uint32_t pos_fp;
    uint32_t step_fp;       // how much to advance per output sample
    int      loop;          // 1 if looping
    // effects
    int      effect_cmd;
    int      effect_param;
    int      porta_target;
    int      porta_speed;
    int      vibrato_pos;
    int      vibrato_speed;
    int      vibrato_depth;
    int      volume_slide;
    int      active;        // 1 if playing a sample
} ModChannel;

// ── Player state ────────────────────────────────────────────────
typedef struct {
    ModFile    *mod;
    ModChannel  ch[MOD_CHANNELS];
    int         pos;        // order table index
    int         row;
    int         tick;
    int         speed;      // ticks per row
    int         tempo;      // BPM
    // sub-tick accumulator (fractional samples between ticks)
    double      tick_accum;
    double      tick_samples; // samples per tick
    int         jump_pos;
    int         jump_row;
    // beat energy (0..64)
    float       beat_energy;
    // output mix buffer scratch
    int         playing;
} ModPlayer;

// ── Parser ──────────────────────────────────────────────────────
static ModFile *mod_load(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);

    uint8_t *raw = (uint8_t*)malloc(size);
    if (!raw) { fclose(f); return NULL; }
    fread(raw, 1, size, f);
    fclose(f);

    ModFile *m = (ModFile*)calloc(1, sizeof(ModFile));
    memcpy(m->title, raw, 20); m->title[20] = 0;

    // 31 instruments
    for (int i = 0; i < MOD_MAX_INSTR; i++) {
        int off = 20 + i * 30;
        ModInstrument *ins = &m->instr[i];
        memcpy(ins->name, raw + off, 22); ins->name[22] = 0;
        ins->length       = ((raw[off+22] << 8) | raw[off+23]) * 2;
        uint8_t ft        = raw[off+24] & 0x0F;
        ins->finetune     = ft > 7 ? (int)ft - 16 : (int)ft;
        ins->volume       = raw[off+25] > 64 ? 64 : raw[off+25];
        ins->repeat_start = ((raw[off+26] << 8) | raw[off+27]) * 2;
        ins->repeat_len   = ((raw[off+28] << 8) | raw[off+29]) * 2;
    }

    m->song_length = raw[950];
    memcpy(m->order, raw + 952, 128);

    int max_pat = 0;
    for (int i = 0; i < m->song_length; i++)
        if (m->order[i] > max_pat) max_pat = m->order[i];
    m->num_patterns = max_pat + 1;

    m->patterns = (ModPattern*)calloc(m->num_patterns, sizeof(ModPattern));

    long off = 1084;
    for (int p = 0; p < m->num_patterns; p++) {
        for (int r = 0; r < MOD_ROWS; r++) {
            for (int c = 0; c < MOD_CHANNELS; c++) {
                uint8_t b0=raw[off],b1=raw[off+1],b2=raw[off+2],b3=raw[off+3];
                off += 4;
                ModNote *n = &m->patterns[p].rows[r][c];
                n->sample = (b0 & 0xF0) | (b2 >> 4);
                n->period = ((b0 & 0x0F) << 8) | b1;
                n->effect = ((b2 & 0x0F) << 8) | b3;
            }
        }
    }

    // Sample data
    for (int i = 0; i < MOD_MAX_INSTR; i++) {
        ModInstrument *ins = &m->instr[i];
        if (ins->length > 0 && off + ins->length <= size) {
            ins->data = (int8_t*)malloc(ins->length);
            memcpy(ins->data, raw + off, ins->length);
            off += ins->length;
        }
    }

    free(raw);
    return m;
}

static void mod_free(ModFile *m) {
    if (!m) return;
    for (int i = 0; i < MOD_MAX_INSTR; i++) free(m->instr[i].data);
    free(m->patterns);
    free(m);
}

// ── Period → playback step ───────────────────────────────────────
// Amiga clock: 3546895 Hz  step = clock / (period * output_rate)
static uint32_t period_to_step(int period, int finetune) {
    if (period <= 0) return 0;
    // Apply finetune (semitone/8 per unit, approximate with power)
    double p = period * pow(2.0, -finetune / 96.0);
    double freq = 3546895.0 / p;
    return (uint32_t)((freq / MOD_SAMPLE_RATE) * 65536.0);
}

// ── Trigger a note on a channel ─────────────────────────────────
static void mod_trigger(ModPlayer *pl, int ci, int period, int sample_offset) {
    ModChannel *ch = &pl->ch[ci];
    if (ch->sample <= 0 || ch->sample > MOD_MAX_INSTR) return;
    ModInstrument *ins = &pl->mod->instr[ch->sample - 1];
    if (!ins->data || ins->length <= 0) return;

    ch->period   = period;
    ch->step_fp  = period_to_step(period, ch->finetune);
    ch->pos_fp   = (uint32_t)(sample_offset) << 16;
    ch->loop     = ins->repeat_len > 2;
    ch->vibrato_pos = 0;
    ch->active   = 1;
}

// ── Process row (tick 0) ─────────────────────────────────────────
static void mod_process_row(ModPlayer *pl) {
    int pat_idx = pl->mod->order[pl->pos];
    ModPattern *pat = &pl->mod->patterns[pat_idx];

    for (int ci = 0; ci < MOD_CHANNELS; ci++) {
        ModNote    *note = &pat->rows[pl->row][ci];
        ModChannel *ch   = &pl->ch[ci];

        int sn  = note->sample;
        int per = note->period;
        int ecmd = (note->effect >> 8) & 0xF;
        int epar =  note->effect       & 0xFF;

        if (sn > 0 && sn <= MOD_MAX_INSTR) {
            ch->sample   = sn;
            ch->volume   = pl->mod->instr[sn-1].volume;
            ch->finetune = pl->mod->instr[sn-1].finetune;
        }

        ch->effect_cmd   = ecmd;
        ch->effect_param = epar;

        switch (ecmd) {
            case 0x3: case 0x5:
                if (per) ch->porta_target = per;
                if (ecmd == 0x3 && epar) ch->porta_speed = epar;
                per = 0; break;
            case 0x9:
                if (per > 0 && ch->sample > 0) mod_trigger(pl, ci, per, epar << 8);
                per = 0; break;
            case 0xC:
                ch->volume = epar > 64 ? 64 : epar; per = 0; break;
            case 0xF:
                if (epar > 0) { if (epar < 32) pl->speed = epar; else { pl->tempo = epar; pl->tick_samples = (MOD_SAMPLE_RATE * 2.5) / pl->tempo; } }
                break;
            case 0xB:
                pl->jump_pos = epar < pl->mod->song_length ? epar : 0;
                pl->jump_row = 0; break;
            case 0xD: {
                int br = ((epar>>4)*10)+(epar&0xF);
                pl->jump_pos = (pl->pos + 1) >= pl->mod->song_length ? 0 : pl->pos + 1;
                pl->jump_row = br > 63 ? 63 : br; break;
            }
            case 0xE: {
                int ex = epar>>4, ey = epar&0xF;
                if (ex == 0xA) { ch->volume = (ch->volume+ey)>64?64:ch->volume+ey; }
                else if (ex == 0xB) { ch->volume = (ch->volume-ey)<0?0:ch->volume-ey; }
                break;
            }
        }

        if (per > 0) {
            mod_trigger(pl, ci, per, 0);
        }
    }
}

// ── Process effects on ticks 1+ ─────────────────────────────────
static void mod_process_fx(ModPlayer *pl) {
    for (int ci = 0; ci < MOD_CHANNELS; ci++) {
        ModChannel *ch = &pl->ch[ci];
        int cmd = ch->effect_cmd, par = ch->effect_param;
        switch (cmd) {
            case 0x1: ch->period -= par; if(ch->period<113)ch->period=113; ch->step_fp=period_to_step(ch->period,ch->finetune); break;
            case 0x2: ch->period += par; if(ch->period>856)ch->period=856; ch->step_fp=period_to_step(ch->period,ch->finetune); break;
            case 0x3: case 0x5:
                if (ch->porta_target && ch->period) {
                    if (ch->period < ch->porta_target) { ch->period += ch->porta_speed; if(ch->period>ch->porta_target)ch->period=ch->porta_target; }
                    else { ch->period -= ch->porta_speed; if(ch->period<ch->porta_target)ch->period=ch->porta_target; }
                    ch->step_fp = period_to_step(ch->period, ch->finetune);
                }
                if (cmd == 0x5) { int u=par>>4,d=par&0xF; ch->volume+=u?u:-d; if(ch->volume>64)ch->volume=64; if(ch->volume<0)ch->volume=0; }
                break;
            case 0x4: case 0x6:
                if (cmd==0x4 && par) { if(par>>4)ch->vibrato_speed=par>>4; if(par&0xF)ch->vibrato_depth=par&0xF; }
                ch->vibrato_pos=(ch->vibrato_pos+ch->vibrato_speed)&63;
                { int delta=(int)(sin(ch->vibrato_pos*3.14159265*2.0/64.0)*ch->vibrato_depth);
                  int vp=ch->period+delta; if(vp<113)vp=113; if(vp>856)vp=856;
                  ch->step_fp=period_to_step(vp, ch->finetune); }
                if (cmd==0x6) { int u=par>>4,d=par&0xF; ch->volume+=u?u:-d; if(ch->volume>64)ch->volume=64; if(ch->volume<0)ch->volume=0; }
                break;
            case 0xA: { int u=par>>4,d=par&0xF; ch->volume+=u?u:-d; if(ch->volume>64)ch->volume=64; if(ch->volume<0)ch->volume=0; break; }
        }
    }
}

// ── Mix one audio frame into stereo int16 buffer ───────────────
// Amiga panning: ch0=L, ch1=R, ch2=R, ch3=L
static void mod_mix(ModPlayer *pl, int16_t *out, int num_samples) {
    static const float PAN[4] = { 0.15f, 0.85f, 0.85f, 0.15f }; // L/R mix

    for (int s = 0; s < num_samples; s++) {
        float L = 0, R = 0;

        // Advance tick
        pl->tick_accum += 1.0;
        while (pl->tick_accum >= pl->tick_samples) {
            pl->tick_accum -= pl->tick_samples;
            if (pl->tick == 0) mod_process_row(pl);
            else               mod_process_fx(pl);
            pl->tick++;
            if (pl->tick >= pl->speed) {
                pl->tick = 0;
                if (pl->jump_pos >= 0) {
                    pl->pos = pl->jump_pos; pl->row = pl->jump_row;
                    pl->jump_pos = -1; pl->jump_row = -1;
                } else {
                    pl->row++;
                    if (pl->row >= MOD_ROWS) {
                        pl->row = 0;
                        pl->pos = (pl->pos + 1) % pl->mod->song_length;
                    }
                }
            }
        }

        for (int ci = 0; ci < MOD_CHANNELS; ci++) {
            ModChannel    *ch  = &pl->ch[ci];
            if (!ch->active || ch->sample <= 0) continue;
            ModInstrument *ins = &pl->mod->instr[ch->sample - 1];
            if (!ins->data) continue;

            int idx = ch->pos_fp >> 16;

            // Loop / end check
            if (ins->repeat_len > 2) {
                int loop_end = ins->repeat_start + ins->repeat_len;
                if (idx >= loop_end) {
                    int over = idx - ins->repeat_start;
                    ch->pos_fp = ((uint32_t)ins->repeat_start << 16) + (ch->pos_fp & 0xFFFF) + ((over % ins->repeat_len) << 16);
                    idx = ch->pos_fp >> 16;
                }
            } else if (idx >= ins->length) {
                ch->active = 0; continue;
            }

            // Linear interpolation
            float samp;
            if (idx + 1 < ins->length) {
                float frac = (ch->pos_fp & 0xFFFF) / 65536.0f;
                samp = ins->data[idx] * (1.0f - frac) + ins->data[idx+1] * frac;
            } else {
                samp = ins->data[idx];
            }
            samp = samp / 128.0f * (ch->volume / 64.0f);

            L += samp * (1.0f - PAN[ci]);
            R += samp * PAN[ci];

            ch->pos_fp += ch->step_fp;
        }

        // Beat energy (simple RMS of output)
        float energy = (fabsf(L) + fabsf(R)) * 0.5f;
        pl->beat_energy = pl->beat_energy * 0.9f + energy * 0.1f;

        // Clamp and write
        L *= 0.6f; R *= 0.6f;
        out[s*2+0] = (int16_t)(L > 1.0f ? 32767 : L < -1.0f ? -32768 : L * 32767.0f);
        out[s*2+1] = (int16_t)(R > 1.0f ? 32767 : R < -1.0f ? -32768 : R * 32767.0f);
    }
}

// ── Init / reset player ──────────────────────────────────────────
static void mod_player_init(ModPlayer *pl, ModFile *mod) {
    memset(pl, 0, sizeof(ModPlayer));
    pl->mod          = mod;
    pl->speed        = 6;
    pl->tempo        = 125;
    pl->tick_samples = (MOD_SAMPLE_RATE * 2.5) / 125.0;
    pl->jump_pos     = -1;
    pl->jump_row     = -1;
    pl->playing      = 1;
}
