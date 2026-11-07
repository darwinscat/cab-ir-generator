// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of Orbit Bass Cab Builder and Orbit Guitar Cab Builder — see LICENSE.

// THE PANEL'S CALLS PLAYED ONTO A CANVAS, as the app's JUCE draws them — the calls of src/web/PanelWeb.cpp (its header
// says what each is). A page may play them through its own painter instead; this is the one they are checked against.
//
//   const panel = new PanelCanvas (module, canvas.getContext ('2d'), { font: 'Michroma', scale: devicePixelRatio });
//   panel.paint ();            // after cabir_panel_dirty () says 1, or every frame while cabir_panel_wants_frames ()
//
// TYPE: the panel's heights are JUCE's — a face's ascent plus its descent — so a CSS size is the height times the face's
// em over its ascent + descent: Michroma's 2048 / (2368 + 544). Measured and drawn a glyph at a time, tracked.

const MICHROMA = { em: 2048 / 2912, ascent: 2368 / 2912, descent: 544 / 2912 };

export class PanelCanvas
{
    constructor (module, ctx, { font = 'Michroma', metrics = MICHROMA, scale = 1, onGlyphsReady = null } = {})
    {
        this.m = module;
        this.ctx = ctx;
        this.font = font;
        this.metrics = metrics;
        this.scale = scale;
        this.onGlyphsReady = onGlyphsReady;
        this.glyphs = new Map ();   // mic + colour -> an Image of its tinted SVG
        this.decoder = new TextDecoder ();
        module.cabirMeasure = (text, height, tracking) => this.measure (text, height, tracking);
        module.cabirAscent = (height) => height * this.metrics.ascent;
    }

    cssFont (height) { return `${height * this.metrics.em}px ${this.font}`; }

    /** The app's Look::trackedWidthPlain: the glyphs' run plus `tracking` x height between each two. */
    measure (text, height, tracking)
    {
        const chars = [...text];
        if (chars.length === 0) return 0;
        this.ctx.save ();
        this.ctx.font = this.cssFont (height);
        const run = this.ctx.measureText (text);
        this.ctx.restore ();
        const ink = (run.actualBoundingBoxRight ?? run.width) + (run.actualBoundingBoxLeft ?? 0);
        return ink + tracking * height * (chars.length - 1);
    }

    paint ()
    {
        const m = this.m, ctx = this.ctx;
        const count = m.ccall ('cabir_panel_paint', 'number', [], []);
        const at = m.ccall ('cabir_panel_ops', 'number', [], []) / 4;
        const textAt = m.ccall ('cabir_panel_text', 'number', [], []);
        const words = m.HEAPU32.subarray (at, at + count);
        const floats = m.HEAPF32.subarray (at, at + count);
        const text = (offset, bytes) => this.decoder.decode (m.HEAPU8.subarray (textAt + offset, textAt + offset + bytes));

        ctx.save ();
        ctx.setTransform (this.scale, 0, 0, this.scale, 0, 0);
        let fill = '#000';
        const colourOf = (argb) =>
            `rgba(${(argb >>> 16) & 255},${(argb >>> 8) & 255},${argb & 255},${((argb >>> 24) & 255) / 255})`;
        const useColour = (style) => { fill = style; ctx.fillStyle = style; ctx.strokeStyle = style; };
        const rounded = (x, y, w, h, c) => { ctx.beginPath (); ctx.roundRect (x, y, w, h, Math.max (0, Math.min (c, w / 2, h / 2))); };
        const ellipse = (x, y, w, h) => { ctx.beginPath (); ctx.ellipse (x + w / 2, y + h / 2, Math.abs (w / 2), Math.abs (h / 2), 0, 0, 2 * Math.PI); };

        for (let i = 0; i < count;)
        {
            const op = words[i++];
            const f = () => floats[i++], u = () => words[i++];
            switch (op)
            {
                case 1: useColour (colourOf (u ())); break;
                case 2:
                {
                    const c1 = u (), x1 = f (), y1 = f (), c2 = u (), x2 = f (), y2 = f (), radial = f ();
                    const g = radial ? ctx.createRadialGradient (x1, y1, 0, x1, y1, Math.hypot (x2 - x1, y2 - y1))
                                     : ctx.createLinearGradient (x1, y1, x2, y2);
                    g.addColorStop (0, colourOf (c1));
                    g.addColorStop (1, colourOf (c2));
                    useColour (g);
                    break;
                }
                case 3: ctx.fillRect (f (), f (), f (), f ()); break;
                case 4: { const x = f (), y = f (), w = f (), h = f (); rounded (x, y, w, h, f ()); ctx.fill (); break; }
                case 5: { const x = f (), y = f (), w = f (), h = f (), c = f (); ctx.lineWidth = f (); rounded (x, y, w, h, c); ctx.stroke (); break; }
                case 6:
                {
                    const x1 = f (), y1 = f (), x2 = f (), y2 = f ();
                    ctx.lineWidth = f (); ctx.lineCap = 'butt';
                    ctx.beginPath (); ctx.moveTo (x1, y1); ctx.lineTo (x2, y2); ctx.stroke ();
                    break;
                }
                case 7: ellipse (f (), f (), f (), f ()); ctx.fill (); break;
                case 8: { const x = f (), y = f (), w = f (), h = f (); ctx.lineWidth = f (); ellipse (x, y, w, h); ctx.stroke (); break; }
                case 9:
                {
                    ctx.lineWidth = f ();
                    ctx.lineJoin = ['miter', 'round', 'bevel'][f ()];
                    ctx.lineCap = ['butt', 'square', 'round'][f ()];
                    const steps = f ();
                    ctx.beginPath ();
                    for (let s = 0; s < steps; ++s)
                    {
                        const kind = f (), a = f (), b = f (), c = f (), d = f (), e = f (), from = f (), to = f (), fresh = f ();
                        if (kind === 0) ctx.moveTo (a, b);
                        else if (kind === 1) ctx.lineTo (a, b);
                        else
                        {
                            // JUCE's angles run clockwise from twelve o'clock; the canvas's from three.
                            if (fresh) ctx.moveTo (a + c * Math.sin (from), b - d * Math.cos (from));
                            ctx.ellipse (a, b, c, d, e, from - Math.PI / 2, to - Math.PI / 2, to < from);
                        }
                    }
                    ctx.stroke ();
                    break;
                }
                case 10: { const x = f (), y = f (), w = f (), h = f (); ctx.beginPath (); ctx.rect (x, y, w, h); ctx.clip (); break; }
                case 11: ctx.save (); break;
                case 12: ctx.restore (); fill = ctx.fillStyle; break;
                case 13:
                {
                    const t = text (u (), u ()), x = f (), y = f (), w = f (), h = f (), height = f (), tracking = f (), flags = u ();
                    this.words (t, x, y, w, h, height, tracking, flags);
                    break;
                }
                case 14:
                {
                    const mic = text (u (), u ()), x = f (), y = f (), w = f (), h = f (), argb = u (), lean = f (), px = f (), py = f ();
                    this.glyph (mic, x, y, w, h, argb, lean, px, py);
                    break;
                }
                default: i = count; break;   // a call this player does not know: stop rather than misread the rest
            }
        }
        ctx.restore ();
    }

    /** The app's Look::drawTrackedPlain. */
    words (t, x, y, w, h, height, tracking, flags)
    {
        const ctx = this.ctx, chars = [...t];
        if (chars.length === 0) return;
        const total = this.measure (t, height, tracking);
        let left = x;
        if (flags & 4) left += (w - total) / 2;
        else if (flags & 2) left += w - total;
        const ascent = height * this.metrics.ascent, descent = height * this.metrics.descent;
        let base = y + h / 2 + (ascent - descent) / 2;
        if (flags & 8) base = y + ascent;
        else if (flags & 16) base = y + h - descent;
        ctx.save ();
        ctx.font = this.cssFont (height);
        ctx.textBaseline = 'alphabetic';
        const step = tracking * height;
        const first = ctx.measureText (chars[0]).actualBoundingBoxLeft ?? 0;
        let run = '';
        chars.forEach ((ch, k) =>
        {
            const advance = run.length ? ctx.measureText (run).width : 0;
            ctx.fillText (ch, left + first + advance + step * k, base);
            run += ch;
        });
        ctx.restore ();
    }

    /** A mic's glyph, its black turned to `argb`, fitted into the box as JUCE's drawWithin centres it, leaning about the pivot. */
    glyph (mic, x, y, w, h, argb, lean, px, py)
    {
        const key = mic + ':' + argb;
        let img = this.glyphs.get (key);
        if (! img)
        {
            const at = this.m.ccall ('cabir_glyph_svg', 'number', ['string'], [mic]);
            if (! at) return;
            const hex = '#' + (argb & 0xffffff).toString (16).padStart (6, '0');
            const svg = this.m.UTF8ToString (at).replaceAll ('#000000', hex);
            img = new Image ();
            img.alpha = ((argb >>> 24) & 255) / 255;
            img.onload = () => { img.ready = true; if (this.onGlyphsReady) this.onGlyphsReady (); };
            img.src = 'data:image/svg+xml;charset=utf-8,' + encodeURIComponent (svg);
            this.glyphs.set (key, img);
        }
        if (! img.ready) return;
        const k = Math.min (w / 36, h / 96), gw = 36 * k, gh = 96 * k;
        const ctx = this.ctx;
        ctx.save ();
        ctx.globalAlpha *= img.alpha;
        ctx.translate (px, py); ctx.rotate (lean); ctx.translate (-px, -py);
        ctx.drawImage (img, x + (w - gw) / 2, y + (h - gh) / 2, gw, gh);
        ctx.restore ();
    }
}
