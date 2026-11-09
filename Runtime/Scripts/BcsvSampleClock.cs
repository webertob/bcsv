// Copyright (c) 2025-2026 Tobias Weber. Licensed under the MIT License.
using System;

namespace BCSV
{
    /// <summary>
    /// Decides when a row is written: a free-running sample clock that reports how
    /// many of its edges fell inside the host step it was just advanced by.
    /// </summary>
    /// <remarks>
    /// <para>
    /// Every recorder built on this library reinvents this piece, because a host
    /// step rate and a wanted row rate are rarely the same number. A simulation
    /// ticking at 1 kHz usually wants fewer rows than that, occasionally wants more,
    /// and in both cases the question is the same one: <i>how many sample instants
    /// fell inside the interval I was just handed?</i> This class answers only that
    /// question. It holds no time base, writes nothing, and knows nothing about a
    /// layout — a recording's time column is the caller's to define, in whatever
    /// unit and width the caller wants.
    /// </para>
    /// <para>
    /// <b>There is one case, not two.</b> It is tempting to split "host faster than
    /// the clock" from "host slower than the clock" and write two paths. They are
    /// the same mechanism, and the only difference is how many edges
    /// <see cref="Advance"/> reports: 0 on most steps when decimating, 1 when the
    /// rates match, k when the host step spans several periods. Callers loop over
    /// <see cref="Edges"/> and never branch on the regime.
    /// </para>
    /// <para>
    /// <b>The phase carries its remainder, and that is the whole point.</b> The
    /// naive decimator resets its accumulator to zero on every edge, which silently
    /// rounds the requested rate down to a divisor of the step rate. Measured on a
    /// 1 kHz host: 300 Hz becomes 250 Hz, 400 Hz becomes 333 Hz, and 999 Hz becomes
    /// 500 Hz — an error of half the requested rate, reported by nothing. Carrying
    /// the remainder instead makes the individual intervals uneven (at 300 Hz on a
    /// 1 kHz host: 4, 3, 3, 4, 3, 3 ms) and the <i>mean</i> rate exact. Uneven and
    /// correct beats even and wrong; a recording that claims 300 Hz should contain
    /// 300 rows per second.
    /// </para>
    /// <para>
    /// <b>Nothing is snapped to a boundary.</b> A phase landing a hair below a
    /// period stays below it and produces its edge on the following step. Rounding
    /// it up looks tidier and is not: it perturbs exactly the slow phase
    /// accumulation that makes the mean rate come out right.
    /// </para>
    /// <para>
    /// One consequence is worth knowing before it surprises someone. A step that
    /// reads as an exact multiple of the period on paper is usually not one in
    /// binary: <c>0.010f</c> is 0.00999999977 s, so advancing a 1 kHz clock by it
    /// reports <b>nine</b> edges, not ten. Nothing is lost — the remainder is
    /// carried and the tenth edge arrives at the start of the next step — but a
    /// caller checking a single advance against arithmetic done by hand will see
    /// the discrepancy. Where the step and the period are the same value, which is
    /// the common case of a sample rate set equal to the host rate, the landing
    /// <i>is</i> exact and the phase returns to zero.
    /// </para>
    /// <para>
    /// <b>Why <see cref="float"/> is enough here.</b> This clock advances by
    /// relative intervals only and never accumulates absolute time, so the phase
    /// stays bounded by one period and the error is a random walk rather than a
    /// drift. Over an hour at a 1 kHz step that is roughly
    /// sqrt(3.6e6) x 1.16e-10 s ~ 2e-7 s of phase error, against a period measured
    /// in milliseconds. The line worth knowing: this holds only because there is no
    /// rate-error model. The moment a clock wants a ppm-class frequency offset or a
    /// non-zero start phase, float quantises the parameter itself and the state has
    /// to become <see cref="double"/>.
    /// </para>
    /// <para>
    /// The first edge falls at the first advance whose accumulated time reaches one
    /// period, not at zero. A caller wanting a row at the instant recording starts
    /// writes that row itself; it is a decision about the recording, not about the
    /// clock.
    /// </para>
    /// </remarks>
    public sealed class BcsvSampleClock
    {
        /// <summary>Largest edge count a single advance can report.</summary>
        /// <remarks>
        /// Not a policy limit — a guard against a quotient that would not survive
        /// the cast to <see cref="int"/>. A caller handing over an interval large
        /// enough to reach it has a bug, and a silently truncated count would hide
        /// it.
        /// </remarks>
        private const float MaxQuotient = 2147483648.0f;

        private readonly float _period;
        private float _phase;

        private BcsvSampleClock(float period)
        {
            _period = period;
        }

        /// <summary>A clock whose edges are <paramref name="seconds"/> apart.</summary>
        /// <param name="seconds">
        /// The period, in seconds. Zero means one edge per advance — the host step
        /// rate, whatever that turns out to be.
        /// </param>
        /// <exception cref="ArgumentOutOfRangeException">
        /// The period is negative, or is not a finite number.
        /// </exception>
        public static BcsvSampleClock FromPeriod(float seconds)
        {
            if (float.IsNaN(seconds) || float.IsInfinity(seconds))
                throw new ArgumentOutOfRangeException(nameof(seconds), seconds,
                    "The sample period must be a finite number of seconds.");
            if (seconds < 0.0f)
                throw new ArgumentOutOfRangeException(nameof(seconds), seconds,
                    "The sample period must not be negative. Zero means one row per host step.");

            return new BcsvSampleClock(seconds);
        }

        /// <summary>A clock that produces <paramref name="hertz"/> edges per second.</summary>
        /// <param name="hertz">
        /// The rate, in Hz. Zero means one edge per advance — the host step rate,
        /// whatever that turns out to be.
        /// </param>
        /// <remarks>
        /// Offered alongside <see cref="FromPeriod"/> rather than making the caller
        /// convert, because hand-converting one spelling into the other is how a
        /// configuration ends up asking for 0.0000500000024 seconds. Say whichever
        /// of the two the requirement is actually written in.
        /// </remarks>
        /// <exception cref="ArgumentOutOfRangeException">
        /// The rate is negative, is not a finite number, or is small enough that its
        /// period is not representable.
        /// </exception>
        public static BcsvSampleClock FromRate(float hertz)
        {
            if (float.IsNaN(hertz) || float.IsInfinity(hertz))
                throw new ArgumentOutOfRangeException(nameof(hertz), hertz,
                    "The sample rate must be a finite number of hertz.");
            if (hertz < 0.0f)
                throw new ArgumentOutOfRangeException(nameof(hertz), hertz,
                    "The sample rate must not be negative. Zero means one row per host step.");

            if (hertz == 0.0f)
                return new BcsvSampleClock(0.0f);

            float period = 1.0f / hertz;
            if (float.IsInfinity(period) || period == 0.0f)
                throw new ArgumentOutOfRangeException(nameof(hertz), hertz,
                    "The sample rate is too small to express as a period in single precision.");

            return new BcsvSampleClock(period);
        }

        /// <summary>
        /// Builds a clock from a rate in hertz without throwing, and says what is
        /// wrong with the rate if it cannot.
        /// </summary>
        /// <param name="hertz">
        /// The rate. Zero means one edge per advance; anything negative, infinite or
        /// NaN is refused.
        /// </param>
        /// <param name="clock">The clock, or null when the rate is unusable.</param>
        /// <param name="problem">
        /// A fragment naming the fault — "is negative", "is infinite", "is not a
        /// number" — meant to be read after the value, or null on success.
        /// </param>
        /// <returns>True if <paramref name="clock"/> was built.</returns>
        /// <remarks>
        /// <para>
        /// The counterpart of <see cref="FromRate"/> for a caller holding a rate it
        /// did not choose — a serialised field, a config file, a value off the wire.
        /// A component driving a game loop cannot let a bad number throw once per
        /// frame, and must not open a file it is then going to abandon, so it needs
        /// to ask before committing to anything.
        /// </para>
        /// <para>
        /// It exists because the obvious inline test is wrong at both ends.
        /// <c>hertz &gt; 0 ? FromRate(hertz) : EveryStep()</c> reads as though it
        /// handles everything, and quietly does not: a negative rate and a NaN both
        /// fail the comparison and come back as "record every step", which is a
        /// plausible-looking rate nobody asked for, while an infinite one passes it
        /// and throws from wherever the caller happened to be.
        /// </para>
        /// </remarks>
        public static bool TryFromRate(float hertz, out BcsvSampleClock clock, out string problem)
        {
            clock = null;
            problem = null;

            if (float.IsNaN(hertz)) { problem = "is not a number"; return false; }
            if (float.IsInfinity(hertz)) { problem = "is infinite"; return false; }
            if (hertz < 0.0f) { problem = "is negative"; return false; }

            if (hertz == 0.0f) { clock = EveryStep(); return true; }

            float period = 1.0f / hertz;
            if (float.IsInfinity(period) || period == 0.0f)
            {
                problem = "is too small to express as a period in single precision";
                return false;
            }

            clock = new BcsvSampleClock(period);
            return true;
        }

        /// <summary>A clock that produces exactly one edge per advance.</summary>
        /// <remarks>
        /// The host step rate, whatever it is. Equivalent to a period or rate of
        /// zero, and spelled out because "record every step" is a request in its own
        /// right rather than a degenerate rate.
        /// </remarks>
        public static BcsvSampleClock EveryStep() => new BcsvSampleClock(0.0f);

        /// <summary>The period between edges, in seconds; zero when free-running.</summary>
        public float Period => _period;

        /// <summary>The rate in hertz; zero when free-running.</summary>
        public float Rate => _period > 0.0f ? 1.0f / _period : 0.0f;

        /// <summary>False when the clock produces one edge per advance.</summary>
        public bool IsPeriodic => _period > 0.0f;

        /// <summary>Edges that fell inside the most recent <see cref="Advance"/>.</summary>
        /// <remarks>0, 1, or k. Zero on most steps whenever the clock is decimating.</remarks>
        public int Edges { get; private set; }

        /// <summary>
        /// Seconds from the last edge to the end of the most recent advance, in
        /// <c>[0, Period)</c>.
        /// </summary>
        /// <remarks>
        /// How far past its sample instant the host step ended. A caller writing the
        /// value it read at the end of the step ignores this; a caller interpolating
        /// onto the edge needs it. Always zero for a free-running clock, which has
        /// no instant of its own to be late for.
        /// </remarks>
        public float Phase => _phase;

        /// <summary>Edges produced since construction or the last <see cref="Reset"/>.</summary>
        /// <remarks>
        /// The row count a recorder driven by this clock will have written, which is
        /// why it is <see cref="ulong"/>: at 1 kHz an <see cref="int"/> runs out
        /// after twenty-five days.
        /// </remarks>
        public ulong Ticks { get; private set; }

        /// <summary>
        /// Where one edge of the most recent advance fell inside that host step, as
        /// a fraction of it.
        /// </summary>
        /// <param name="index">
        /// Which edge, from 0 (earliest in the step) to <see cref="Edges"/> - 1
        /// (latest).
        /// </param>
        /// <param name="dt">The interval the most recent <see cref="Advance"/> was given.</param>
        /// <returns>
        /// A fraction in <c>(0, 1]</c>: 0 is the start of the step, 1 its end.
        /// </returns>
        /// <remarks>
        /// <para>
        /// A caller reading its values once per host step has them at the step
        /// boundaries, while an edge generally falls somewhere between two of them.
        /// This is the weight that places the edge: <c>prev + f * (cur - prev)</c>.
        /// </para>
        /// <para>
        /// Worth being clear about what interpolation between two host samples can
        /// and cannot do. It aligns a recording to a wanted rate, and it invents no
        /// information: content above the host's own Nyquist frequency was never
        /// sampled, so no filter applied afterwards can recover it. Interpolating
        /// upward is rate alignment, not anti-aliasing — the direction where
        /// filtering genuinely helps is downward, where every host sample was seen
        /// and can be averaged rather than discarded.
        /// </para>
        /// <para>
        /// A free-running clock returns 1: its single edge is the end of the step,
        /// which is where the caller read, so interpolation degenerates to the value
        /// itself.
        /// </para>
        /// </remarks>
        /// <exception cref="ArgumentOutOfRangeException">
        /// <paramref name="index"/> is outside the edges the last advance reported.
        /// </exception>
        public float EdgeFraction(int index, float dt)
        {
            if (index < 0 || index >= Edges)
                throw new ArgumentOutOfRangeException(nameof(index), index,
                    "The last advance reported " + Edges + " edges.");

            if (!(dt > 0.0f) || float.IsInfinity(dt))
                return 1.0f;

            // Edges sit one period apart, ending Phase seconds before the end of the
            // step; index 0 is the earliest, so it is (Edges - 1) periods further back.
            float fromEnd = _phase + (Edges - 1 - index) * _period;
            float f = (dt - fromEnd) / dt;

            // Only against round-off: in exact arithmetic every edge reported by the
            // last advance lies inside the step it was advanced by.
            if (f < 0.0f) return 0.0f;
            if (f > 1.0f) return 1.0f;
            return f;
        }

        /// <summary>Returns the clock to its starting phase and clears the counters.</summary>
        public void Reset()
        {
            _phase = 0.0f;
            Edges = 0;
            Ticks = 0UL;
        }

        /// <summary>
        /// Advances the clock by one host step and reports how many edges fell
        /// inside it.
        /// </summary>
        /// <param name="dt">
        /// The host interval, in seconds. Zero is legitimate — a step during which
        /// no time passed produces no edges and leaves the phase where it was.
        /// </param>
        /// <returns>
        /// The number of edges inside this step: 0, 1, or k. Also readable
        /// afterwards as <see cref="Edges"/>.
        /// </returns>
        /// <remarks>
        /// Throws rather than absorbing a bad interval. A driver handing over a
        /// negative or infinite step has a bug, and an interval quietly swallowed
        /// here becomes an unexplainable row count weeks later with nothing pointing
        /// at the cause. A timeline that moved backwards is a seek, and a seek
        /// rebases a clock rather than advancing it — call <see cref="Reset"/>.
        /// Callers driving this from a game loop, where an exception per frame is
        /// its own kind of failure, should validate at that layer and report once.
        /// </remarks>
        /// <exception cref="ArgumentOutOfRangeException">
        /// <paramref name="dt"/> is negative or is not a finite number, or is large
        /// enough that the edge count would not fit an <see cref="int"/>.
        /// </exception>
        public int Advance(float dt)
        {
            if (float.IsNaN(dt) || float.IsInfinity(dt))
                throw new ArgumentOutOfRangeException(nameof(dt), dt,
                    "The host interval must be a finite number of seconds.");
            if (dt < 0.0f)
                throw new ArgumentOutOfRangeException(nameof(dt), dt,
                    "The host interval must not be negative. A timeline that moved " +
                    "backwards is a seek, not an advance — reset the clock instead.");

            if (!IsPeriodic)
            {
                // No period of its own: one edge per advance, in phase with whatever
                // drives it. This is what a rate of zero means, and several callers
                // configure it deliberately.
                Edges = dt > 0.0f ? 1 : 0;
                _phase = 0.0f;
                Ticks += (ulong)Edges;
                return Edges;
            }

            if (dt == 0.0f)
            {
                // No time passed, so no edge can have fallen inside the step. The
                // phase is deliberately left alone: it is state, not a per-step
                // result, and clearing it here would shift every later edge.
                Edges = 0;
                return 0;
            }

            _phase += dt;

            // Subtract whole periods, and snap nothing onto a boundary. The phase is
            // non-negative and the period is positive, so the quotient is
            // non-negative and truncation is the floor.
            float quotient = _phase / _period;
            if (quotient >= MaxQuotient)
                throw new ArgumentOutOfRangeException(nameof(dt), dt,
                    "The host interval spans more sample periods than an edge count can hold.");

            int whole = (int)quotient;
            _phase -= whole * _period;

            // Only against round-off in the subtraction itself: in exact arithmetic
            // the phase cannot go below zero, so this can trim a value a few ulps
            // under it and nothing else.
            if (_phase < 0.0f)
                _phase = 0.0f;

            Edges = whole;
            Ticks += (ulong)whole;
            return whole;
        }
    }
}
