// Copyright (c) 2025-2026 Tobias Weber. Licensed under the MIT License.
using System;
using System.IO;
using BCSV;
using NUnit.Framework;
using UnityEngine;
using UnityEngine.TestTools;

namespace BCSV.Tests
{
    /// <summary>
    /// Component-level tests for <see cref="BcsvRecorder"/>.
    /// </summary>
    /// <remarks>
    /// The arithmetic these components rest on is covered where it can run
    /// without an editor: <c>BcsvSampleClock</c> and its rate validation are
    /// tested in the standalone C# suite. What is left here is what genuinely
    /// needs Unity — a MonoBehaviour's lifecycle, its serialised fields, and the
    /// native plugin underneath it. Every test drives the components through
    /// <c>Pacing.External</c> rather than waiting on <c>FixedUpdate</c>, so
    /// nothing here depends on the editor's frame timing.
    /// </remarks>
    public class BcsvRecorderTests
    {
        private string _dir;
        private GameObject _go;

        [SetUp]
        public void SetUp()
        {
            _dir = Path.Combine(Path.GetTempPath(), "bcsv_unity_tests_" + Guid.NewGuid().ToString("N").Substring(0, 8));
            Directory.CreateDirectory(_dir);
            _go = new GameObject("recorder");
        }

        [TearDown]
        public void TearDown()
        {
            if (_go != null) UnityEngine.Object.DestroyImmediate(_go);
            if (Directory.Exists(_dir)) Directory.Delete(_dir, true);
        }

        private BcsvRecorder NewRecorder(string file = "r.bcsv")
        {
            var rec = _go.AddComponent<BcsvRecorder>();
            rec.recordOnStart = false;              // Start() must not open anything
            rec.pacing = BcsvRecorder.Pacing.External;
            rec.outputPath = Path.Combine(_dir, file);
            return rec;
        }

        // ── Rate validation ─────────────────────────────────────────────

        /// <summary>
        /// A rate that cannot be honoured must be refused before the file is
        /// opened. Building the clock after the writer meant an infinite rate
        /// threw with a native handle already in hand, out of Start().
        /// </summary>
        [TestCase(float.NaN)]
        [TestCase(float.PositiveInfinity)]
        [TestCase(-1.0f)]
        public void AnUnusableSampleRateRefusesToOpenAFile(float bad)
        {
            var rec = NewRecorder();
            rec.Track("x", () => 1.0f);
            rec.sampleRateHz = bad;

            LogAssert.ignoreFailingMessages = true;
            Assert.IsFalse(rec.BeginRecording(), "an unusable rate must not start a recording");
            LogAssert.ignoreFailingMessages = false;

            Assert.IsFalse(rec.IsRecording);
            Assert.IsFalse(File.Exists(rec.outputPath), "no file should have been created");
        }

        [Test]
        public void AnUnusableRateSetWhileRecordingKeepsThePreviousOne()
        {
            var rec = NewRecorder();
            rec.Track("x", () => 1.0f);
            rec.sampleRateHz = 100.0f;
            Assert.IsTrue(rec.BeginRecording());

            rec.sampleRateHz = float.NaN;
            LogAssert.ignoreFailingMessages = true;
            for (int i = 0; i < 100; i++) rec.Advance(0.001f);
            LogAssert.ignoreFailingMessages = false;

            Assert.IsTrue(rec.IsRecording, "recording must survive a bad rate");
            Assert.AreEqual(10, rec.RowsWritten, "it must still be running at 100 Hz");
            rec.EndRecording();
        }

        // ── Lifecycle ───────────────────────────────────────────────────

        [Test]
        public void RecordingWithNothingSubscribedIsRefused()
        {
            var rec = NewRecorder();
            LogAssert.ignoreFailingMessages = true;
            Assert.IsFalse(rec.BeginRecording());
            LogAssert.ignoreFailingMessages = false;
        }

        [Test]
        public void AnUnknownRowCodecIsRefusedBeforeAFileIsCreated()
        {
            var rec = NewRecorder();
            rec.Track("x", () => 1.0f);
            rec.rowCodec = "zho";

            LogAssert.ignoreFailingMessages = true;
            Assert.IsFalse(rec.BeginRecording());
            LogAssert.ignoreFailingMessages = false;
            Assert.IsFalse(File.Exists(rec.outputPath));
        }

        [Test]
        public void ADuplicateChannelNameIsRefused()
        {
            var rec = NewRecorder();
            rec.Track("x", () => 1.0f);
            LogAssert.ignoreFailingMessages = true;
            rec.Track("x", () => 2.0f);
            LogAssert.ignoreFailingMessages = false;
            Assert.AreEqual(1, rec.SubscriptionCount);
        }

        [Test]
        public void SubscriptionsAreRefusedWhileAFileIsOpen()
        {
            var rec = NewRecorder();
            rec.Track("x", () => 1.0f);
            Assert.IsTrue(rec.BeginRecording());

            LogAssert.ignoreFailingMessages = true;
            rec.Track("y", () => 2.0f);
            LogAssert.ignoreFailingMessages = false;

            Assert.AreEqual(1, rec.SubscriptionCount);
            rec.EndRecording();
        }

        // ── Decimation ──────────────────────────────────────────────────

        [Test]
        public void TheMeanRateIsTheRateThatWasAskedFor()
        {
            var rec = NewRecorder();
            rec.Track("x", () => 1.0f);
            rec.sampleRateHz = 300.0f;                  // not a divisor of 1 kHz
            Assert.IsTrue(rec.BeginRecording());

            for (int i = 0; i < 1000; i++) rec.Advance(0.001f);
            rec.EndRecording();

            // 250 is what a decimator that resets its accumulator produces.
            Assert.AreEqual(300, rec.RowsWritten, 1);
        }

        // ── Filtering ───────────────────────────────────────────────────

        /// <summary>
        /// A dropped row must not leave some averaging windows closed and others
        /// open: the channels either side of the failure would then average
        /// different spans of time, which is a silent corruption of exactly the
        /// data the filter exists to get right.
        /// </summary>
        [Test]
        public void ARowDroppedByAFailingGetterLeavesEveryAverageWindowIntact()
        {
            var rec = NewRecorder();
            int step = 0;
            bool boom = false;

            rec.Track("a0", () => (float)step, BcsvRecorder.Sampling.Average);
            rec.Track("bang", () => boom ? throw new InvalidOperationException("boom") : 0.0f);
            rec.Track("a1", () => (float)step, BcsvRecorder.Sampling.Average);
            rec.sampleRateHz = 0.0f;                    // a row every step
            Assert.IsTrue(rec.BeginRecording());

            // One step that fails, then one that succeeds.
            step = 1; boom = true;
            LogAssert.ignoreFailingMessages = true;
            rec.Advance(0.001f);
            LogAssert.ignoreFailingMessages = false;
            Assert.AreEqual(1, rec.FailedRows);

            step = 3; boom = false;
            rec.Advance(0.001f);
            Assert.AreEqual(1, rec.RowsWritten);
            string path = rec.CurrentPath;

            // EndRecording reports the dropped row as an error, and Unity fails a
            // test on any Debug.LogError it was not told to expect.
            LogAssert.ignoreFailingMessages = true;
            rec.EndRecording();
            LogAssert.ignoreFailingMessages = false;

            // Both averaged steps 1 and 3, so both must read 2.
            using (var reader = new BcsvReader())
            {
                reader.Open(path);
                Assert.IsTrue(reader.ReadNext());
                Assert.AreEqual(2.0f, reader.Row.GetFloat(0), 1e-4f, "a0 window");
                Assert.AreEqual(2.0f, reader.Row.GetFloat(2), 1e-4f, "a1 window");
            }
        }

        /// <summary>
        /// The failing channel is itself averaged, and it sits between two other
        /// averaged channels — so the throw happens during the per-step read
        /// rather than during the row write, and lands with one filtered channel
        /// already read and one not.
        ///
        /// The earlier version of this test used a Latest channel to fail, which
        /// meant every filtered read had already succeeded before the failure and
        /// the per-step path was never exercised at all.
        /// </summary>
        [Test]
        public void AFailingFilteredGetterMovesNoFilterStateAtAll()
        {
            var rec = NewRecorder();
            int step = 0;
            bool boom = false;

            rec.Track("a0", () => (float)step, BcsvRecorder.Sampling.Average);
            rec.Track("bang", () => boom ? throw new InvalidOperationException("boom") : (float)step,
                      BcsvRecorder.Sampling.Average);
            rec.Track("a1", () => (float)step, BcsvRecorder.Sampling.Average);
            rec.sampleRateHz = 0.0f;
            Assert.IsTrue(rec.BeginRecording());
            Assert.IsTrue(rec.SamplesEveryStep);

            step = 1; boom = true;
            LogAssert.ignoreFailingMessages = true;
            rec.Advance(0.001f);
            LogAssert.ignoreFailingMessages = false;
            Assert.AreEqual(0, rec.RowsWritten, "the failing step wrote nothing");

            step = 3; boom = false;
            rec.Advance(0.001f);
            Assert.AreEqual(1, rec.RowsWritten);
            string path = rec.CurrentPath;

            LogAssert.ignoreFailingMessages = true;
            rec.EndRecording();
            LogAssert.ignoreFailingMessages = false;

            // Step 1 was never taken by any channel, so every window holds step 3
            // alone. A channel that had kept step 1 would read 2.
            using (var reader = new BcsvReader())
            {
                reader.Open(path);
                Assert.IsTrue(reader.ReadNext());
                Assert.AreEqual(3.0f, reader.Row.GetFloat(0), 1e-4f, "a0 read before the failure");
                Assert.AreEqual(3.0f, reader.Row.GetFloat(1), 1e-4f, "the failing channel itself");
                Assert.AreEqual(3.0f, reader.Row.GetFloat(2), 1e-4f, "a1 read after the failure");
            }
        }

        [Test]
        public void AveragingSeesEveryHostStepAndNotOnlyTheRecordedOnes()
        {
            var rec = NewRecorder();

            // A plain read of `step`, not something that counts its own calls:
            // BeginRecording primes each filtered channel with one read, so a
            // getter with a side effect sees one more call than there were host
            // steps. That primed value is outside the first window, which is part
            // of what this asserts.
            int step = 0;
            rec.Track("avg", () => (float)step, BcsvRecorder.Sampling.Average);
            rec.sampleRateHz = 100.0f;                  // one row per ten steps
            Assert.IsTrue(rec.BeginRecording());
            Assert.IsTrue(rec.SamplesEveryStep);

            for (int i = 1; i <= 10; i++) { step = i; rec.Advance(0.001f); }
            string path = rec.CurrentPath;
            rec.EndRecording();

            using (var reader = new BcsvReader())
            {
                reader.Open(path);
                Assert.IsTrue(reader.ReadNext());
                // Steps 1..10 were all seen; their mean is 5.5, not the 10 a
                // zero-order hold would have recorded.
                Assert.AreEqual(5.5f, reader.Row.GetFloat(0), 1e-4f);
            }
        }

        [Test]
        public void ALatestOnlyRecordingDoesNotReadItsGettersOnSilentSteps()
        {
            var rec = NewRecorder();
            int reads = 0;
            rec.Track("x", () => { reads++; return 1.0f; });
            rec.sampleRateHz = 100.0f;
            Assert.IsTrue(rec.BeginRecording());
            Assert.IsFalse(rec.SamplesEveryStep);

            for (int i = 0; i < 100; i++) rec.Advance(0.001f);
            rec.EndRecording();

            Assert.AreEqual(10, reads, "a Latest channel is read only where a row is due");
        }
    }
}
