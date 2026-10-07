/*
 * render — pure, Android-independent software compositor.
 *
 * Operates on a caller-provided 32-bit RGBX_8888 pixel buffer, so it can run
 * both inside the app (on a locked ANativeWindow buffer) and on the host (for
 * tests that dump a PNG). No Android, GL, font or image libraries.
 */
#ifndef DJI_EVASIVE_RENDER_H
#define DJI_EVASIVE_RENDER_H

#include <stdint.h>
#include "netinfo.h"
#include "sysinfo.h"

/* Top strip reserved for the Android status bar / OS inset. Nothing drawn or
 * tapped above this: content, the logo, and the WebUI button all start below it,
 * since the OS owns this region (taps there are eaten by the system). */
#define RENDER_OS_TOP 100
/* Extra breathing room below the OS strip before content. */
#define RENDER_TOP_MARGIN (RENDER_OS_TOP + 8)

/* A 32-bit pixel surface. `stride` is in pixels (not bytes). Pixel layout is
 * RGBX_8888: low byte R, then G, then B, high byte X/A. */
typedef struct {
    uint32_t *bits;
    int32_t   width;
    int32_t   height;
    int32_t   stride;
} surface_t;

/* Decoded logo alpha-mask (from the .tkimg asset). */
typedef struct {
    uint32_t w, h;
    uint32_t rgb;       /* 0x00RRGGBB */
    const uint8_t *alpha;  /* w*h coverage bytes, not owned */
} logo_mask_t;

/* Parse a .tkimg buffer into `out` (alpha points into `data`). Returns 1 on
 * success, 0 if malformed. Does not copy the alpha data. */
int logo_parse(const uint8_t *data, long len, logo_mask_t *out);

/* A generic text section (e.g. CONTROL, FORWARDS, TRAFFIC). Header lines have
 * no leading space and are drawn in the accent colour; data lines begin with
 * two spaces. Each line may carry its own colour and a sparkline. */
#define RENDER_EXTRA_MAX   64
#define RENDER_EXTRA_LEN   104
#define RENDER_SPARK_SLOTS 30   /* sparkline width in samples */
typedef struct {
    char            lines[RENDER_EXTRA_MAX][RENDER_EXTRA_LEN];
    uint32_t        colour[RENDER_EXTRA_MAX];   /* 0x00RRGGBB, 0 = default */
    const uint32_t *spark[RENDER_EXTRA_MAX];    /* values, oldest first */
    int             spark_n[RENDER_EXTRA_MAX];
    int             spark_col;  /* character column where sparklines start */
    int             count;
} textblock_t;

/* Empty a block. */
void textblock_clear(textblock_t *tb);

/* Append a printf-formatted line in `colour` (0 = default). Returns its index,
 * or -1 if the block is full. */
int textblock_add(textblock_t *tb, uint32_t colour, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

/* Real-time flight data (decoded from OSD). Zero/invalid until the decoder
 * lands; when !valid the flight widgets show a "NO TELEMETRY" state. */
typedef struct {
    int    valid;        /* 1 = GPS fix (lat/lon/alt usable) */
    int    att_valid;    /* 1 = attitude/heading usable (OSD decoded & fresh);
                          * independent of GPS — the horizon + heading gauge use
                          * this, so they render indoors without a fix. */
    double lat, lon;     /* deg */
    double alt_m;        /* altitude MSL (m) */
    float  roll_deg;     /* + = right wing down */
    float  pitch_deg;    /* + = nose up */
    float  yaw_deg;      /* 0..360 true heading */
    int    sats;         /* GNSS sats, -1 unknown */
} flightdata_t;

/* Heading gauge modes. */
#define HEADING_NORTH_UP    0   /* N fixed at top, aircraft marker rotates */
#define HEADING_AIRCRAFT_UP 1   /* current heading at top, rose rotates */

/* ---- Pure geometry helpers (no pixels; unit-tested) ---------------------*/

/* Endpoints of the artificial-horizon line in a gauge of radius `r` centred
 * at (cx,cy), in screen coordinates (y down). Level flight gives a horizontal
 * line through the centre. Positive pitch (nose up) drops the horizon below
 * centre; positive roll (right wing down) raises its right end. Endpoints lie
 * on the gauge circle (x0,y0 = left end, x1,y1 = right end). */
void horizon_points(int cx, int cy, int r, float pitch_deg, float roll_deg,
                    int *x0, int *y0, int *x1, int *y1);

/* Normalise any angle to [0,360). */
float heading_norm(float deg);

/* Screen angle (degrees clockwise from the top of the gauge, [0,360)) at which
 * the compass bearing `tick_deg` is drawn. North-up: bearing == angle.
 * Aircraft-up: the current heading sits at the top. */
float heading_tick_angle(float heading_deg, int mode, int tick_deg);

/* Tap zone (top-right corner, labelled "WEB UI") that returns to the WebView
 * UI. Fills x,y,w,h for a surface of width `sw` x height `sh`. */
void render_webui_zone(int sw, int sh, int *x, int *y, int *w, int *h);

/* Pure hit-test: 1 if (px,py) lies inside the rect (x,y,w,h), edges on the
 * top/left inclusive and bottom/right exclusive; 0 otherwise (or w/h <= 0). */
int rect_contains(int x, int y, int w, int h, int px, int py);

/* Compose a full frame on a black background:
 *   - `logo` (optional) centred at the top; content starts below it, or at
 *     the top edge when `logo` is NULL;
 *   - left column: `sys`, then `extra`, then the NEIGHBOURS part of `ni`;
 *   - right column: the INTERFACES part of `ni`;
 *   - `wide`, full width, below both columns.
 * When `fd` is non-NULL a flight-data band (horizon + heading gauge) is drawn
 * at the top and the columns start below it, with the flight readout at the
 * left of the columns. Any argument except `s` may be NULL (that section is skipped). */
void render_frame(surface_t *s, const logo_mask_t *logo,
                  const sysinfo_t *sys, const netinfo_t *ni,
                  const textblock_t *extra, const textblock_t *wide,
                  const flightdata_t *fd);

#endif /* DJI_EVASIVE_RENDER_H */
