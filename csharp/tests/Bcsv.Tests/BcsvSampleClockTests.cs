// Copyright (c) 2025-2026 Tobias Weber. Licensed under the MIT License.
using Xunit;

namespace Bcsv.Tests;

/// <summary>
/// The defect these tests exist for is a decimator that resets its accumulator
/// to zero on every edge instead of carrying the remainder. It is an easy thing
/// to write, it looks right, and it silently rounds the requested rate down to a
/// divisor of the host step rate — at 1 kHz, asking for 999 Hz gets 500 Hz, an
/// error of half the requested rate that nothing reports. It has been written at
/// least twice in this workspace.
///
/// So the assertions here are about the rate that comes out over time, not about
/// any individual interval: with the carry, the intervals are deliberately
/// uneven and the mean is exact. A test that pinned the intervals instead would
/// be asserting the bug.
/// </summary>
public class BcsvSampleClockTests
{
    /// <summary>A 1 kHz host step, the rate this workspace's simulations run at.</summary>
    private const float Step = 0.001f;

    /// <summary>Runs <paramref name="steps"/> host steps and returns the total edges.</summary>
    private static ulong Run(BcsvSampleClock clock, int steps, float dt = Step)
    {
        for (int i = 0; i < steps; i++)
            clock.Advance(dt);
        return clock.Ticks;
    }

    // ── The regression the class exists for ──────────────────────────────

    [Theory]
    // requested rate, edges expected in one second, what reset-to-zero produces
    [InlineData(300.0f, 300, 250)]
    [InlineData(400.0f, 400, 333)]
    [InlineData(999.0f, 999, 500)]
    [InlineData(100.0f, 100, 100)]   // a divisor: the naive version is right here, which is why it survives review
    [InlineData(1000.0f, 1000, 1000)] // the host step rate itself
    public void MeanRateIsTheRequestedRateEvenWhenItIsNotADivisorOfTheStep(
        float hertz, int expected, int whatResettingToZeroWouldGive)
    {
        var clock = BcsvSampleClock.FromRate(hertz);

        ulong edges = Run(clock, steps: 1000);   // one second at 1 kHz

        Assert.InRange(edges, (ulong)(expected - 1), (ulong)(expected + 1));

        // Stated separately and deliberately: the point is not only that the
        // count is right but that it is nowhere near the aliased one.
        if (expected != whatResettingToZeroWouldGive)
            Assert.NotInRange(edges, (ulong)(whatResettingToZeroWouldGive - 1),
                                     (ulong)(whatResettingToZeroWouldGive + 1));
    }

    /// <summary>
    /// What carrying the remainder actually looks like: at 300 Hz on a 1 kHz
    /// host, every gap is 3 or 4 steps and never anything else. Resetting to
    /// zero produces a uniform 4 and the rate error that comes with it.
    /// </summary>
    [Fact]
    public void TheGapsAreUnevenAndBracketTheExactRatio()
    {
        var clock = BcsvSampleClock.FromRate(300.0f);
        var gaps = new List<int>();

        int sinceLastEdge = 0;
        for (int i = 0; i < 1000; i++)
        {
            sinceLastEdge++;
            if (clock.Advance(Step) > 0)
            {
                gaps.Add(sinceLastEdge);
                sinceLastEdge = 0;
            }
        }

        Assert.All(gaps, g => Assert.True(g == 3 || g == 4,
            $"gap of {g} steps: with the carry, 300 Hz on a 1 kHz host is only ever 3 or 4"));
        Assert.Contains(3, gaps);
        Assert.Contains(4, gaps);
        Assert.InRange(gaps.Average(), 3.32, 3.35);   // 1000/300 = 3.3333
    }

    /// <summary>
    /// The empirical backing for keeping the phase in single precision. An hour
    /// at 1 kHz is 3.6 million advances; if float were accumulating drift rather
    /// than a bounded random walk, this is where it would show.
    /// </summary>
    [Fact]
    public void ThePhaseDoesNotDriftOverAnHourOfSteps()
    {
        var clock = BcsvSampleClock.FromRate(300.0f);

        ulong edges = Run(clock, steps: 3_600_000);   // one hour at 1 kHz

        // 3600 s x 300 Hz = 1,080,000. A tenth of a percent is 1080 rows; the
        // bound here is far tighter, and the aliased 250 Hz answer would be
        // 900,000.
        Assert.InRange(edges, 1_079_900UL, 1_080_100UL);
    }

    // ── The one-case-not-two property ────────────────────────────────────

    /// <summary>
    /// A 10 ms step at a 1 kHz sample rate: the edges inside it come back from
    /// one call, and the caller writes that many rows. Same loop as the
    /// decimating case, no branch on the regime.
    ///
    /// The bound is 9-or-10 rather than exactly 10, and that is the class
    /// behaving correctly: <c>0.010f</c> is 0.00999999977 s, which is genuinely
    /// a hair under ten periods of <c>0.001f</c>. Snapping it up would report a
    /// row that did not happen. The deficit is carried rather than lost, which
    /// is what the second half of this test pins down.
    /// </summary>
    [Fact]
    public void AStepLongerThanThePeriodReportsEveryEdgeInsideIt()
    {
        var clock = BcsvSampleClock.FromRate(1000.0f);

        Assert.InRange(clock.Advance(0.010f), 9, 10);

        // And the shortfall does not compound: a hundred such steps is still a
        // thousand rows to within the one edge that the interval itself is short.
        for (int i = 0; i < 99; i++)
            clock.Advance(0.010f);
        Assert.InRange(clock.Ticks, 999UL, 1000UL);
    }

    /// <summary>
    /// The case a consumer is most likely to configure — sample rate equal to
    /// the host step rate — is exact, not merely close: one row per step, and
    /// the phase returns to zero rather than creeping.
    /// </summary>
    [Fact]
    public void ARateMatchingTheStepRateLandsExactly()
    {
        var clock = BcsvSampleClock.FromRate(1000.0f);

        for (int i = 0; i < 1000; i++)
            Assert.Equal(1, clock.Advance(Step));

        Assert.Equal(1000UL, clock.Ticks);
        Assert.Equal(0.0f, clock.Phase);
    }

    [Fact]
    public void MostStepsReportNoEdgeAtAllWhenDecimating()
    {
        var clock = BcsvSampleClock.FromRate(100.0f);   // every tenth step at 1 kHz

        int silent = 0;
        for (int i = 0; i < 100; i++)
            if (clock.Advance(Step) == 0) silent++;

        Assert.Equal(90, silent);
        Assert.Equal(10UL, clock.Ticks);
    }

    // ── Free-running ─────────────────────────────────────────────────────

    [Fact]
    public void AFreeRunningClockEmitsExactlyOneEdgePerAdvance()
    {
        var clock = BcsvSampleClock.EveryStep();

        Assert.False(clock.IsPeriodic);
        for (int i = 0; i < 10; i++)
            Assert.Equal(1, clock.Advance(Step));
        Assert.Equal(10UL, clock.Ticks);
    }

    [Fact]
    public void AFreeRunningClockIgnoresHowLongTheStepWas()
    {
        var clock = BcsvSampleClock.EveryStep();

        // It has no period to compare against, so a long step is still one row.
        Assert.Equal(1, clock.Advance(10.0f));
        Assert.Equal(1, clock.Advance(1e-9f));
    }

    [Theory]
    [InlineData(0.0f)]
    public void ZeroMeansEveryStepInBothSpellings(float zero)
    {
        Assert.False(BcsvSampleClock.FromRate(zero).IsPeriodic);
        Assert.False(BcsvSampleClock.FromPeriod(zero).IsPeriodic);
        Assert.Equal(1, BcsvSampleClock.FromRate(zero).Advance(Step));
        Assert.Equal(1, BcsvSampleClock.FromPeriod(zero).Advance(Step));
    }

    // ── Phase ────────────────────────────────────────────────────────────

    [Fact]
    public void PhaseIsHowFarPastTheEdgeTheStepEnded()
    {
        var clock = BcsvSampleClock.FromPeriod(0.010f);

        clock.Advance(0.004f);
        Assert.Equal(0, clock.Edges);
        Assert.Equal(0.004f, clock.Phase, 6);

        // Crosses at 10 ms and ends 2 ms later.
        Assert.Equal(1, clock.Advance(0.008f));
        Assert.Equal(0.002f, clock.Phase, 5);
        Assert.InRange(clock.Phase, 0.0f, clock.Period);
    }

    [Fact]
    public void AZeroLengthStepProducesNothingAndLeavesThePhaseAlone()
    {
        var clock = BcsvSampleClock.FromPeriod(0.010f);
        clock.Advance(0.004f);

        Assert.Equal(0, clock.Advance(0.0f));
        Assert.Equal(0.004f, clock.Phase, 6);

        // And the edge still lands where it would have: 6 ms later, not 10.
        Assert.Equal(1, clock.Advance(0.006f));
    }

    [Fact]
    public void AFreeRunningClockHasNoPhaseOfItsOwn()
    {
        var clock = BcsvSampleClock.EveryStep();
        clock.Advance(0.004f);
        Assert.Equal(0.0f, clock.Phase);
    }

    // ── Construction ─────────────────────────────────────────────────────

    [Fact]
    public void RateAndPeriodAreTwoSpellingsOfOneClock()
    {
        var byRate = BcsvSampleClock.FromRate(250.0f);
        var byPeriod = BcsvSampleClock.FromPeriod(0.004f);

        Assert.Equal(byPeriod.Period, byRate.Period, 6);
        Assert.Equal(byRate.Rate, byPeriod.Rate, 3);
        Assert.Equal(Run(byRate, 1000), Run(byPeriod, 1000));
    }

    [Theory]
    [InlineData(float.NaN)]
    [InlineData(float.PositiveInfinity)]
    [InlineData(-1.0f)]
    public void ConstructionRejectsARateThatIsNotAFinitePositiveNumber(float bad)
    {
        Assert.Throws<ArgumentOutOfRangeException>(() => BcsvSampleClock.FromRate(bad));
        Assert.Throws<ArgumentOutOfRangeException>(() => BcsvSampleClock.FromPeriod(bad));
    }

    [Fact]
    public void ConstructionRejectsARateTooSmallToExpressAsAPeriod()
    {
        Assert.Throws<ArgumentOutOfRangeException>(() => BcsvSampleClock.FromRate(float.Epsilon));
    }

    // ── TryFromRate ──────────────────────────────────────────────────────

    /// <summary>
    /// A component holding a rate it did not choose — a serialised field, a
    /// config value — cannot let a bad number throw once per frame, and must not
    /// open a file it is about to abandon.
    ///
    /// The reason this is a method rather than an inline comparison is that the
    /// obvious inline comparison is wrong at both ends, and was wrong in shipped
    /// code: <c>hertz &gt; 0 ? FromRate(hertz) : EveryStep()</c> turns a negative
    /// rate and a NaN into "every step" — a plausible rate nobody asked for —
    /// while letting an infinite one through to throw.
    /// </summary>
    [Theory]
    [InlineData(float.NaN, "is not a number")]
    [InlineData(float.PositiveInfinity, "is infinite")]
    [InlineData(float.NegativeInfinity, "is infinite")]
    [InlineData(-1.0f, "is negative")]
    [InlineData(-0.0001f, "is negative")]
    public void ARateThatIsNotFiniteAndNonNegativeIsRefusedRatherThanCoerced(float bad, string why)
    {
        BcsvSampleClock clock;
        string problem;

        Assert.False(BcsvSampleClock.TryFromRate(bad, out clock, out problem));
        Assert.Null(clock);
        Assert.Equal(why, problem);
    }

    [Fact]
    public void ARateTooSmallToExpressAsAPeriodIsRefused()
    {
        BcsvSampleClock clock;
        string problem;
        Assert.False(BcsvSampleClock.TryFromRate(float.Epsilon, out clock, out problem));
        Assert.Null(clock);
        Assert.NotNull(problem);
    }

    [Fact]
    public void ZeroIsAcceptedAndMeansEveryStep()
    {
        BcsvSampleClock clock;
        string problem;

        Assert.True(BcsvSampleClock.TryFromRate(0.0f, out clock, out problem));
        Assert.Null(problem);
        Assert.False(clock.IsPeriodic);
        Assert.Equal(1, clock.Advance(Step));
    }

    [Fact]
    public void AUsableRateBuildsTheSameClockAsFromRate()
    {
        BcsvSampleClock tried;
        string problem;

        Assert.True(BcsvSampleClock.TryFromRate(300.0f, out tried, out problem));
        Assert.Null(problem);
        Assert.Equal(BcsvSampleClock.FromRate(300.0f).Period, tried.Period, 6);

        // And it decimates identically: one second at 1 kHz is 300 rows.
        Assert.InRange(Run(tried, steps: 1000), 299UL, 301UL);
    }

    // ── Advance validation ───────────────────────────────────────────────

    [Theory]
    [InlineData(float.NaN)]
    [InlineData(float.PositiveInfinity)]
    [InlineData(float.NegativeInfinity)]
    [InlineData(-0.001f)]
    public void AdvanceRefusesAnIntervalThatIsNotPositiveAndFinite(float bad)
    {
        var clock = BcsvSampleClock.FromRate(100.0f);
        Assert.Throws<ArgumentOutOfRangeException>(() => clock.Advance(bad));
        Assert.Throws<ArgumentOutOfRangeException>(() => BcsvSampleClock.EveryStep().Advance(bad));
    }

    [Fact]
    public void AdvanceRefusesAnIntervalSpanningMoreEdgesThanACountCanHold()
    {
        var clock = BcsvSampleClock.FromPeriod(1e-6f);
        Assert.Throws<ArgumentOutOfRangeException>(() => clock.Advance(1e6f));
    }

    [Fact]
    public void ARefusedAdvanceChangesNothing()
    {
        var clock = BcsvSampleClock.FromPeriod(0.010f);
        clock.Advance(0.004f);

        Assert.Throws<ArgumentOutOfRangeException>(() => clock.Advance(-1.0f));

        Assert.Equal(0.004f, clock.Phase, 6);
        Assert.Equal(0UL, clock.Ticks);
    }

    // ── EdgeFraction ─────────────────────────────────────────────────────

    /// <summary>
    /// A caller reads its values at host step boundaries; an edge falls between
    /// two of them. This is the weight that places it, and getting it wrong
    /// would put every interpolated sample at the wrong instant while still
    /// producing a plausible-looking recording.
    /// </summary>
    [Fact]
    public void AnEdgeAtTheEndOfTheStepWeighsEntirelyOnTheCurrentValue()
    {
        var clock = BcsvSampleClock.FromPeriod(0.010f);

        Assert.Equal(1, clock.Advance(0.010f));
        Assert.Equal(0.0f, clock.Phase, 6);
        Assert.Equal(1.0f, clock.EdgeFraction(0, 0.010f), 5);
    }

    [Fact]
    public void AnEdgeHalfwayThroughTheStepWeighsHalfEach()
    {
        var clock = BcsvSampleClock.FromPeriod(0.010f);
        clock.Advance(0.005f);            // phase 5 ms, no edge yet

        // Crosses 10 ms at the midpoint of this 10 ms step, ending 5 ms past it.
        Assert.Equal(1, clock.Advance(0.010f));
        Assert.Equal(0.005f, clock.Phase, 6);
        Assert.Equal(0.5f, clock.EdgeFraction(0, 0.010f), 4);
    }

    /// <summary>
    /// Several edges in one step are reported earliest-first, and their weights
    /// must come back in that order: an off-by-one here would reverse a burst of
    /// interpolated rows in time.
    /// </summary>
    [Fact]
    public void SeveralEdgesInOneStepAreWeightedEarliestFirst()
    {
        var clock = BcsvSampleClock.FromPeriod(0.005f);

        Assert.Equal(4, clock.Advance(0.020f));

        float[] f =
        {
            clock.EdgeFraction(0, 0.020f),
            clock.EdgeFraction(1, 0.020f),
            clock.EdgeFraction(2, 0.020f),
            clock.EdgeFraction(3, 0.020f),
        };

        Assert.Equal(0.25f, f[0], 4);
        Assert.Equal(0.50f, f[1], 4);
        Assert.Equal(0.75f, f[2], 4);
        Assert.Equal(1.00f, f[3], 4);
    }

    [Fact]
    public void EveryWeightLiesInsideTheStepItCameFrom()
    {
        var clock = BcsvSampleClock.FromRate(377.0f);   // deliberately not a divisor

        for (int i = 0; i < 500; i++)
        {
            int edges = clock.Advance(Step);
            for (int e = 0; e < edges; e++)
                Assert.InRange(clock.EdgeFraction(e, Step), 0.0f, 1.0f);
        }
    }

    [Fact]
    public void AFreeRunningEdgeIsTheEndOfTheStep()
    {
        var clock = BcsvSampleClock.EveryStep();
        clock.Advance(Step);

        // Its one edge is where the caller read, so there is nothing to
        // interpolate towards.
        Assert.Equal(1.0f, clock.EdgeFraction(0, Step));
    }

    [Theory]
    [InlineData(-1)]
    [InlineData(1)]
    public void AskingForAnEdgeTheLastAdvanceDidNotProduceIsRefused(int index)
    {
        var clock = BcsvSampleClock.FromPeriod(0.010f);
        Assert.Equal(1, clock.Advance(0.010f));   // exactly one edge, so only index 0 exists
        Assert.Throws<ArgumentOutOfRangeException>(() => clock.EdgeFraction(index, 0.010f));
    }

    // ── Reset ────────────────────────────────────────────────────────────

    [Fact]
    public void ResetReturnsTheClockToItsStartingPhase()
    {
        var clock = BcsvSampleClock.FromPeriod(0.010f);
        Run(clock, 25, dt: 0.004f);
        Assert.True(clock.Ticks > 0UL);

        clock.Reset();

        Assert.Equal(0UL, clock.Ticks);
        Assert.Equal(0, clock.Edges);
        Assert.Equal(0.0f, clock.Phase);

        // And it decimates from scratch rather than from wherever it was.
        Assert.Equal(0, clock.Advance(0.004f));
    }
}
