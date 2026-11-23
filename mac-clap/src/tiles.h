#pragma once

#include "hdslave_tiles.h"
#include "screen.h"

namespace foundry
{

struct TilesAccess
{
    static constexpr int kTileCount = 6; // 3x2 grid

    static constexpr uint32_t kKitDeckIds[18] = {
        M::volume,    M::kitTune,     M::kitDecay,
        M::accent,    M::afxFilter,   M::afxReso,
        M::afxDrive,  M::clip,        M::humanize,
        M::comp,      M::compMix,     M::scSource,
        M::dlyLevel,  M::dlyTime,     M::dlyFeedback,
        M::revLevel,  M::revTime,     M::revTone
    };

    static uint32_t CLAP_ABI count (const clap_plugin_t*, int* out_cols, int* out_rows)
    {
        if (out_cols) *out_cols = 3;
        if (out_rows) *out_rows = 2;
        return kTileCount;
    }

    static const char* CLAP_ABI title (const clap_plugin_t*, uint32_t tile)
    {
        static constexpr const char* kTitles[kTileCount] = {
            "01 // DRUM PADS & STRIKE",
            "02 // VOICE CIRCUIT (1-12)",
            "03 // VOICE FILTER & DRIVE (13-24)",
            "04 // VOICE SENDS & LFO (25-36)",
            "05 // 16-VOICE LEVEL MIXER",
            "06 // KIT BUS / COMP / FX"
        };
        return tile < kTileCount ? kTitles[tile] : "";
    }

    static void drawPadStrip (const Paper& g, int selectedPad)
    {
        g.box (0, 0, 320, 34, Screen::night);
        char titleBuf[48];
        const int v = padVoice[selectedPad];
        std::snprintf (titleBuf, sizeof (titleBuf), "A%02d %s", selectedPad + 1, voiceSpecs[v].name);
        g.text (6, 3, titleBuf, Screen::acid, 140, true);
        for (int p = 0; p < voiceCount; ++p)
        {
            const int px = 4 + p * 19;
            const bool sel = (p == selectedPad);
            g.box (px, 17, 18, 14, sel ? Screen::acid : 0xff26343d);
            g.text (px + 2, 19, voiceSpecs[padVoice[p]].tag, sel ? Screen::night : Screen::paper);
        }
        g.box (0, 33, 320, 1, 0xff3e626f);
    }

    static void drawParamCard (const Paper& g, const std::atomic<float>* values, int voiceIdx, uint32_t paramId,
                               int x, int y, int w, int h)
    {
        g.box (x, y, w, h, 0xff162129);
        g.box (x, y, w, 1, 0xff324856);
        const int idx = paramIndex (paramId);
        if (idx < 0) return;
        const auto& row = table().rows[idx];
        const float val = values[idx].load (std::memory_order_relaxed);
        const char* name = row.name;
        if (paramId >= voiceIdBase && voiceIdx >= 0)
        {
            const size_t tagLen = std::strlen (voiceSpecs[voiceIdx].tag);
            if (std::strncmp (name, voiceSpecs[voiceIdx].tag, tagLen) == 0 && name[tagLen] == ' ')
                name += tagLen + 1;
        }
        g.text (x + 4, y + (h >= 30 ? 3 : 2), name, Screen::quiet, w - 8, true);
        char buf[48];
        formatParam (row, val, buf, sizeof (buf));
        g.text (x + 4, y + (h >= 34 ? 15 : 11), buf, Screen::paper, w - 8, false);
        const float norm = row.max > row.min ? std::clamp ((val - row.min) / (row.max - row.min), 0.0f, 1.0f) : 0.0f;
        const int barW = w - 8;
        const int fill = (int) std::lround (norm * (float) barW);
        g.box (x + 4, y + h - 3, barW, 2, 0xff26343d);
        g.box (x + 4, y + h - 3, std::max (1, fill), 2, Screen::acid);
    }

    static bool CLAP_ABI paint (const clap_plugin_t* plugin, uint32_t tile, const melodee_screen_canvas_t* canvas, clap_id knobs_out[3])
    {
        if (tile >= kTileCount || !canvas || !canvas->pixels) return false;
        auto& p = of (plugin);

        if (tile == 0)
        {
            melodee_screen_canvas_t c = *canvas;
            c.whole = true;
            p.screen.dirty = true;
            const bool drew = p.screen.paint (c, p.values, p.meter, true);
            if (knobs_out)
                p.screen.knobs (knobs_out);
            return drew;
        }

        Paper g { canvas->pixels, canvas->stride };
        g.box (0, 0, 320, 181, Screen::night);
        drawPadStrip (g, p.screen.pad);
        const int v = p.screen.voice();
        const int first = paramIndex (voiceId (v, V::level));
        const int vRows = p.screen.voiceRows();

        if (tile >= 1 && tile <= 3)
        {
            const int baseOffset = ((int) tile - 1) * 12;
            for (int i = 0; i < 12; ++i)
            {
                const int r = i / 3, c = i % 3;
                const int cx = 4 + c * 105, cy = 35 + r * 36;
                const int rowIdx = baseOffset + i;
                if (rowIdx < vRows)
                {
                    const uint32_t pid = table().rows[first + rowIdx].id;
                    drawParamCard (g, p.values, v, pid, cx, cy, 102, 34);
                    if (knobs_out && i < 3) knobs_out[i] = pid;
                }
                else if (knobs_out && i < 3)
                {
                    knobs_out[i] = CLAP_INVALID_ID;
                }
            }
            return true;
        }

        if (tile == 4)
        {
            // 16-Voice Level Mixer (4x4 grid in MPC pad layout)
            for (int padIdx = 0; padIdx < voiceCount; ++padIdx)
            {
                const int col = padIdx % 4;
                const int rowFromTop = 3 - (padIdx / 4);
                const int cx = 4 + col * 79, cy = 35 + rowFromTop * 36;
                const int pv = padVoice[padIdx];
                const uint32_t lid = voiceId (pv, V::level);
                const int lIdx = paramIndex (lid);
                const auto& prow = table().rows[lIdx];
                const float val = p.values[lIdx].load (std::memory_order_relaxed);
                const bool sel = (padIdx == p.screen.pad);

                g.box (cx, cy, 76, 34, sel ? 0xff1f3642 : 0xff162129);
                g.box (cx, cy, 76, 1, sel ? Screen::acid : 0xff324856);
                char tagBuf[16];
                std::snprintf (tagBuf, sizeof (tagBuf), "A%02d %s", padIdx + 1, voiceSpecs[pv].tag);
                g.text (cx + 4, cy + 3, tagBuf, sel ? Screen::acid : Screen::quiet, 70, true);
                char valBuf[32];
                formatParam (prow, val, valBuf, sizeof (valBuf));
                g.text (cx + 4, cy + 15, valBuf, Screen::paper, 70, false);
                const float norm = std::clamp ((val - prow.min) / (prow.max - prow.min), 0.0f, 1.0f);
                g.box (cx + 4, cy + 30, 68, 2, 0xff26343d);
                g.box (cx + 4, cy + 30, std::max (1, (int) std::lround (norm * 68.0f)), 2, Screen::acid);
            }
            if (knobs_out)
            {
                knobs_out[0] = voiceId (v, V::level);
                knobs_out[1] = voiceId (v, V::pan);
                knobs_out[2] = voiceId (v, V::tune);
            }
            return true;
        }

        // Tile 5: Kit Bus / Compressor / FX (3x6 grid = 18 cards)
        for (int i = 0; i < 18; ++i)
        {
            const int r = i / 3, c = i % 3;
            const int cx = 4 + c * 105, cy = 35 + r * 24;
            drawParamCard (g, p.values, -1, kKitDeckIds[i], cx, cy, 102, 23);
            if (knobs_out && i < 3) knobs_out[i] = kKitDeckIds[i];
        }
        return true;
    }

    static bool CLAP_ABI hit_test (const clap_plugin_t* plugin, uint32_t tile, int x, int y, hdslave_tile_hotspot_t* out_spot)
    {
        if (!out_spot || tile >= kTileCount) return false;
        *out_spot = {};
        out_spot->param_id = CLAP_INVALID_ID;
        out_spot->param_id_y = CLAP_INVALID_ID;

        auto& p = of (plugin);
        const int v = p.screen.voice();

        if (tile == 0)
        {
            // 16 Drum Pads (x: 7..131, y: 54..125)
            if (x >= 7 && x < 131 && y >= 54 && y < 126)
            {
                const int col = std::clamp ((x - 7) / 31, 0, 3);
                const int rowFromTop = std::clamp ((y - 54) / 18, 0, 3);
                const int padIdx = (3 - rowFromTop) * 4 + col;
                out_spot->kind = HDSLAVE_HOTSPOT_ACTION;
                out_spot->custom_id = padIdx;
                out_spot->x = (int16_t) (7 + col * 31);
                out_spot->y = (int16_t) (54 + rowFromTop * 18);
                out_spot->w = 28;
                out_spot->h = 15;
                return true;
            }
            // MODEL 808 / 909 / 606 line (x: 7..131, y: 126..142)
            if (x >= 7 && x < 131 && y >= 126 && y < 142)
            {
                out_spot->kind = HDSLAVE_HOTSPOT_PARAM;
                out_spot->param_id = voiceId (v, V::model);
                out_spot->x = 7; out_spot->y = 126; out_spot->w = 124; out_spot->h = 15;
                return true;
            }
            // Strike artwork (x: 133..315, y: 32..141): click auditions selected pad!
            if (x >= 133 && x < 315 && y >= 32 && y < 142)
            {
                out_spot->kind = HDSLAVE_HOTSPOT_ACTION;
                out_spot->custom_id = p.screen.pad;
                out_spot->x = 133; out_spot->y = 32; out_spot->w = 182; out_spot->h = 109;
                return true;
            }
            return false;
        }

        // Top 16-pad quick selector strip on Tiles 1..5 (y: 16..33)
        if (y >= 16 && y < 34 && x >= 4 && x < 4 + 16 * 19)
        {
            const int padIdx = std::clamp ((x - 4) / 19, 0, 15);
            out_spot->kind = HDSLAVE_HOTSPOT_ACTION;
            out_spot->custom_id = padIdx;
            out_spot->x = (int16_t) (4 + padIdx * 19);
            out_spot->y = 17;
            out_spot->w = 18;
            out_spot->h = 14;
            return true;
        }

        if (tile >= 1 && tile <= 3 && y >= 35 && y < 179)
        {
            const int c = std::clamp ((x - 4) / 105, 0, 2);
            const int r = std::clamp ((y - 35) / 36, 0, 3);
            const int rowIdx = ((int) tile - 1) * 12 + r * 3 + c;
            const int first = paramIndex (voiceId (v, V::level));
            if (rowIdx < p.screen.voiceRows())
            {
                out_spot->kind = HDSLAVE_HOTSPOT_PARAM;
                out_spot->param_id = table().rows[first + rowIdx].id;
                out_spot->x = (int16_t) (4 + c * 105);
                out_spot->y = (int16_t) (35 + r * 36);
                out_spot->w = 102;
                out_spot->h = 34;
                return true;
            }
            return false;
        }

        if (tile == 4 && y >= 35 && y < 179)
        {
            const int col = std::clamp ((x - 4) / 79, 0, 3);
            const int rowFromTop = std::clamp ((y - 35) / 36, 0, 3);
            const int padIdx = (3 - rowFromTop) * 4 + col;
            const int pv = padVoice[padIdx];
            out_spot->kind = HDSLAVE_HOTSPOT_PARAM;
            out_spot->param_id = voiceId (pv, V::level);
            out_spot->x = (int16_t) (4 + col * 79);
            out_spot->y = (int16_t) (35 + rowFromTop * 36);
            out_spot->w = 76;
            out_spot->h = 34;
            return true;
        }

        if (tile == 5 && y >= 35 && y < 179)
        {
            const int c = std::clamp ((x - 4) / 105, 0, 2);
            const int r = std::clamp ((y - 35) / 24, 0, 5);
            const int idx = r * 3 + c;
            out_spot->kind = HDSLAVE_HOTSPOT_PARAM;
            out_spot->param_id = kKitDeckIds[idx];
            out_spot->x = (int16_t) (4 + c * 105);
            out_spot->y = (int16_t) (35 + r * 24);
            out_spot->w = 102;
            out_spot->h = 23;
            return true;
        }

        return false;
    }

    static void CLAP_ABI focus (const clap_plugin_t* plugin, uint32_t tile, int x, int y, bool)
    {
        auto& p = of (plugin);
        if (tile == 4 && y >= 35 && y < 179)
        {
            const int col = std::clamp ((x - 4) / 79, 0, 3);
            const int rowFromTop = std::clamp ((y - 35) / 36, 0, 3);
            p.screen.pad = (3 - rowFromTop) * 4 + col;
            p.screen.dirty = true;
        }
    }

    static void CLAP_ABI action (const clap_plugin_t* plugin, uint32_t, int32_t custom_id, int32_t, bool)
    {
        auto& p = of (plugin);
        if (custom_id >= 0 && custom_id < voiceCount)
        {
            p.screen.pad = custom_id;
            p.screen.bank = 0;
            p.screen.dirty = true;
            p.auditions.push (p.screen.voice());
            if (p.host && p.host->request_process)
                p.host->request_process (p.host);
            p.askPaint();
        }
    }
};

const clap_plugin_hdslave_tiles_t tilesExtension {
    TilesAccess::count,
    TilesAccess::title,
    TilesAccess::paint,
    TilesAccess::hit_test,
    TilesAccess::focus,
    TilesAccess::action,
    181,
};

} // namespace foundry
