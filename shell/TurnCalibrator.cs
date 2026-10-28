namespace MacroShell;

/* One-button turn calibration (#80), like Razer's Perfect 180.
   Calibrate: press the key, turn one full circle in game, press it again.
   The physical mouse's raw X counts in between are countsPer360.
   Test: each press sends one full turn of the counts the page gave.
   The key is swallowed from the first press until Cancel. A held key's
   repeats are not presses. Injected moves and the board's own moves are
   not the user's hand, so they are not counted. */
public sealed class TurnCalibrator
{
    public enum Phase { Off, Armed, Turning, Testing }

    /* A full turn under this many counts is a missed turn, not a result. */
    public const int MinCounts = 100;
    public const int MaxCounts = 1_000_000_000;
    /* The engines' per-report limit. A test turn goes out in pieces this big. */
    public const int Piece = 16000;

    long _sum;
    bool _held;

    public Phase State { get; private set; }
    public int Vk { get; private set; }
    public int TestCounts { get; private set; }

    /* The counts so far, as a positive number. */
    public int Counts => (int)Math.Min(Math.Abs(_sum), MaxCounts);

    public void Arm(int vk)
    {
        Vk = vk;
        State = Phase.Armed;
        _sum = 0;
        _held = false;
    }

    public void Test(int vk, int counts)
    {
        Vk = vk;
        TestCounts = counts;
        State = Phase.Testing;
        _held = false;
    }

    public void Cancel()
    {
        State = Phase.Off;
        _sum = 0;
        _held = false;
    }

    public bool Swallows(int vk) => State != Phase.Off && vk == Vk;

    /* The key's down or up. A down returns "turning", "done", "short" (the turn
       was under MinCounts, try again), "test", or "" for a repeat. */
    public string Key(int vk, bool down)
    {
        if (!Swallows(vk))
            return "";
        if (!down)
        {
            _held = false;
            return "";
        }
        if (_held)
            return "";
        _held = true;
        switch (State)
        {
            case Phase.Armed:
                _sum = 0;
                State = Phase.Turning;
                return "turning";
            case Phase.Turning:
                if (Counts < MinCounts)
                {
                    State = Phase.Armed;
                    _sum = 0;
                    return "short";
                }
                State = Phase.Off;
                return "done";
            case Phase.Testing:
                return "test";
        }
        return "";
    }

    /* One raw mouse move. device 0 is injected input. */
    public void Raw(int dx, bool absolute, bool injected, bool board)
    {
        if (State != Phase.Turning || absolute || injected || board)
            return;
        _sum += dx;
    }

    /* counts split into pieces of at most Piece, with the sign of counts. */
    public static List<int> Pieces(int counts)
    {
        var list = new List<int>();
        var left = (long)counts;
        while (left != 0)
        {
            var take = Math.Clamp(left, -Piece, Piece);
            list.Add((int)take);
            left -= take;
        }
        return list;
    }
}
