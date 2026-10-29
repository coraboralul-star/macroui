namespace MacroShell;

/* Physical pixels, like GetMonitorInfo and a per-monitor aware GetCursorPos. */
readonly record struct PixelRect(int Left, int Top, int Right, int Bottom)
{
    public int Width => Right - Left;
    public int Height => Bottom - Top;
}

static class RadialPlacement
{
    /* Ring pixels on a monitor: the size at 100 % times that monitor's scale. */
    public static int Pixels(int size, uint dpi) =>
        (int)Math.Round(Math.Clamp(size, RadialView.SizeMin, RadialView.SizeMax) * (dpi == 0 ? 96 : dpi) / 96.0);

    /* Where the ring goes. center is the middle of the game's monitor. cursor
       is centred on the cursor, then moved in so the whole ring stays on that
       monitor. A ring bigger than the monitor is centred on it. */
    public static PixelRect Place(PixelRect monitor, uint dpi, string at, int cursorX, int cursorY, int size)
    {
        var px = Pixels(size, dpi);
        int cx;
        int cy;
        if (at == "cursor")
        {
            cx = cursorX;
            cy = cursorY;
        }
        else
        {
            cx = monitor.Left + monitor.Width / 2;
            cy = monitor.Top + monitor.Height / 2;
        }
        var left = Fit(cx - px / 2, monitor.Left, monitor.Right, px);
        var top = Fit(cy - px / 2, monitor.Top, monitor.Bottom, px);
        return new PixelRect(left, top, left + px, top + px);
    }

    static int Fit(int start, int lo, int hi, int length)
    {
        if (length >= hi - lo)
            return lo + (hi - lo - length) / 2;
        return Math.Clamp(start, lo, hi - length);
    }
}
