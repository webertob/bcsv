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
    /// Component-level tests for <see cref="BcsvPlayer"/>, including the
    /// round trip that matters most: what the recorder wrote is what the player
    /// hands back.
    /// </summary>
    public class BcsvPlayerTests
    {
        private string _dir;
        private GameObject _go;

        [SetUp]
        public void SetUp()
        {
            _dir = Path.Combine(Path.GetTempPath(), "bcsv_unity_player_" + Guid.NewGuid().ToString("N").Substring(0, 8));
            Directory.CreateDirectory(_dir);
            _go = new GameObject("player");
        }

        [TearDown]
        public void TearDown()
        {
            if (_go != null) UnityEngine.Object.DestroyImmediate(_go);
            if (Directory.Exists(_dir)) Directory.Delete(_dir, true);
        }

        /// <summary>Writes a recording with the real recorder and returns its path.</summary>
        private string Record(int rows, string file = "p.bcsv")
        {
            var host = new GameObject("rec");
            try
            {
                var rec = host.AddComponent<BcsvRecorder>();
                rec.recordOnStart = false;
                rec.pacing = BcsvRecorder.Pacing.External;
                rec.outputPath = Path.Combine(_dir, file);

                int i = 0;
                rec.Track("i", () => i);
                rec.Track("x", () => i * 1.5f);
                rec.Track("label", () => "row" + i);
                Assert.IsTrue(rec.BeginRecording());

                for (i = 0; i < rows; i++) rec.Advance(0.001f);
                string path = rec.CurrentPath;
                rec.EndRecording();
                return path;
            }
            finally { UnityEngine.Object.DestroyImmediate(host); }
        }

        private BcsvPlayer NewPlayer(string path)
        {
            var p = _go.AddComponent<BcsvPlayer>();
            p.playOnStart = false;
            p.pacing = BcsvPlayer.Pacing.External;
            p.inputPath = path;
            return p;
        }

        // ── Round trip ──────────────────────────────────────────────────

        /// <summary>
        /// The pairing the two components exist for: every value the recorder
        /// pulled out of a scene comes back through the player, in order, with
        /// its type intact.
        /// </summary>
        [Test]
        public void EverythingTheRecorderWroteComesBackThroughThePlayer()
        {
            string path = Record(50);
            var player = NewPlayer(path);

            int i = -1; float x = 0; string label = null;
            player.BindInt32("i", v => i = v)
                  .BindFloat("x", v => x = v)
                  .BindString("label", v => label = v);

            Assert.IsTrue(player.BeginPlayback());
            Assert.AreEqual(50, player.RowCount);

            for (int n = 0; n < 50; n++)
            {
                Assert.IsTrue(player.Trigger(), "row " + n);
                Assert.AreEqual(n, i);
                Assert.AreEqual(n * 1.5f, x, 1e-4f);
                Assert.AreEqual("row" + n, label);
            }
            player.EndPlayback();
        }

        // ── Completion ──────────────────────────────────────────────────

        /// <summary>
        /// Completion has to land on the last row, not on the edge after it.
        /// Checking "is there a next row" first leaves IsPlaying true for a whole
        /// sample interval past the visible end — a second at 1 Hz, and forever
        /// for a one-row recording nothing steps again.
        /// </summary>
        [Test]
        public void ARecordingFinishesOnItsLastRowRatherThanTheEdgeAfterIt()
        {
            string path = Record(1);
            var player = NewPlayer(path);
            player.playbackRateHz = 1.0f;

            int completed = 0;
            player.Completed += () => completed++;
            player.BindInt32("i", v => { });
            Assert.IsTrue(player.BeginPlayback());

            Assert.AreEqual(1, player.Advance(1.0f), "the one row is presented");
            Assert.IsFalse(player.IsPlaying, "and the recording is finished at once");
            Assert.AreEqual(1, completed, "Completed fires on that row");

            Assert.AreEqual(0, player.Advance(1.0f));
            Assert.AreEqual(1, completed, "and never fires twice");
            player.EndPlayback();
        }

        /// <summary>
        /// Seek reaches the completion contract by a different route than Step,
        /// and has to honour it the same way: landing on the last row is that row
        /// having been presented.
        /// </summary>
        [Test]
        public void SeekingToTheLastRowFinishesTheRecording()
        {
            string path = Record(20);
            var player = NewPlayer(path);

            int completed = 0, i = -1;
            player.Completed += () => completed++;
            player.BindInt32("i", v => i = v);
            Assert.IsTrue(player.BeginPlayback());

            Assert.IsTrue(player.Seek(19));
            Assert.AreEqual(19, i, "the last row was presented");
            Assert.IsFalse(player.IsPlaying, "and the recording is finished");
            Assert.AreEqual(1, completed, "without waiting for another edge");
            player.EndPlayback();
        }

        [Test]
        public void SeekingBackFromTheEndResumesRatherThanStayingFinished()
        {
            string path = Record(20);
            var player = NewPlayer(path);
            int completed = 0, i = -1;
            player.Completed += () => completed++;
            player.BindInt32("i", v => i = v);
            Assert.IsTrue(player.BeginPlayback());

            Assert.IsTrue(player.Seek(19));
            Assert.AreEqual(1, completed);

            Assert.IsTrue(player.Seek(5), "a seek away from the end still works");
            Assert.AreEqual(5, i);
            player.Play();
            Assert.IsTrue(player.Trigger());
            Assert.AreEqual(6, i, "and play continues from there");
            player.EndPlayback();
        }

        [Test]
        public void LoopingWrapsAndNeverReportsCompletion()
        {
            string path = Record(10);
            var player = NewPlayer(path);
            player.loop = true;

            int completed = 0, last = -1;
            player.Completed += () => completed++;
            player.BindInt32("i", v => last = v);
            Assert.IsTrue(player.BeginPlayback());

            for (int n = 0; n < 10; n++) player.Trigger();
            Assert.AreEqual(9, last);
            Assert.IsTrue(player.Trigger());
            Assert.AreEqual(0, last, "the next row wraps to the start");
            Assert.IsTrue(player.IsPlaying);
            Assert.AreEqual(0, completed);
            player.EndPlayback();
        }

        // ── Binding resolution ──────────────────────────────────────────

        [Test]
        public void ABindingNamingAColumnTheFileDoesNotHaveStopsPlayback()
        {
            string path = Record(5);
            var player = NewPlayer(path);
            player.BindInt32("i", v => { });
            player.BindFloat("nope", v => { });

            LogAssert.ignoreFailingMessages = true;
            Assert.IsFalse(player.BeginPlayback());
            LogAssert.ignoreFailingMessages = false;
            Assert.IsFalse(player.IsOpen);
        }

        [Test]
        public void ABindingWithTheWrongTypeStopsPlayback()
        {
            string path = Record(5);
            var player = NewPlayer(path);
            player.BindDouble("x", v => { });     // the file has it as Float

            LogAssert.ignoreFailingMessages = true;
            Assert.IsFalse(player.BeginPlayback());
            LogAssert.ignoreFailingMessages = false;
        }

        // ── Rate validation ─────────────────────────────────────────────

        [TestCase(float.NaN)]
        [TestCase(float.PositiveInfinity)]
        [TestCase(-1.0f)]
        public void AnUnusablePlaybackRateRefusesToOpenTheFile(float bad)
        {
            string path = Record(5);
            var player = NewPlayer(path);
            player.playbackRateHz = bad;
            player.BindInt32("i", v => { });

            LogAssert.ignoreFailingMessages = true;
            Assert.IsFalse(player.BeginPlayback());
            LogAssert.ignoreFailingMessages = false;
            Assert.IsFalse(player.IsOpen);
        }

        // ── Seeking ─────────────────────────────────────────────────────

        [Test]
        public void SeekLandsExactlyAndPlayContinuesFromThere()
        {
            string path = Record(100);
            var player = NewPlayer(path);
            int i = -1;
            player.BindInt32("i", v => i = v);
            Assert.IsTrue(player.BeginPlayback());

            Assert.IsTrue(player.Seek(60));
            Assert.AreEqual(60, i);
            Assert.IsTrue(player.Trigger());
            Assert.AreEqual(61, i);

            LogAssert.ignoreFailingMessages = true;
            Assert.IsFalse(player.Seek(1000), "out of range");
            LogAssert.ignoreFailingMessages = false;
            player.EndPlayback();
        }

        // ── Several rows in one step ────────────────────────────────────

        [Test]
        public void EveryRowDueInOneStepIsDeliveredNotJustTheLast()
        {
            string path = Record(20);
            var player = NewPlayer(path);
            player.playbackRateHz = 4000.0f;      // four rows per 1 kHz step

            var seen = new System.Collections.Generic.List<int>();
            player.BindInt32("i", v => seen.Add(v));
            Assert.IsTrue(player.BeginPlayback());

            Assert.AreEqual(4, player.Advance(0.001f));
            CollectionAssert.AreEqual(new[] { 0, 1, 2, 3 }, seen);
            player.EndPlayback();
        }
    }
}
