// Copyright (c) 2025-2026 Tobias Weber. Licensed under the MIT License.
using System;
using System.Collections.Generic;
using UnityEngine;
using UnityEngine.SceneManagement;

namespace BCSV
{
    /// <summary>
    /// Records values from a running scene into a BCSV file, one row per sample
    /// instant.
    /// </summary>
    /// <remarks>
    /// <para>
    /// A recorder needs two things: somewhere to get values, and somewhere to
    /// put them. This component owns the second half completely and stays
    /// deliberately ignorant of the first: a subscription is a name and a
    /// getter, so nothing here knows what a sensor, a joint or a controller is,
    /// and adding a new kind of thing to record costs this package no new type.
    /// </para>
    /// <para>
    /// <b>It does not write a time column.</b> What a recording calls time — a
    /// simulation clock, a host timestamp, a step counter, seconds or
    /// microseconds, float or double — is a property of the experiment and not
    /// of the file format. Track it like any other channel:
    /// <c>Track("t", () =&gt; Time.fixedTimeAsDouble)</c>. A recorder that
    /// picked the unit and the width for you would be wrong for somebody, and
    /// silently: single precision runs out of millisecond resolution after
    /// about two hours of scene time.
    /// </para>
    /// <para>
    /// <b>Execution order is 1000, and nothing should sit after it.</b> Unity
    /// does not define the order of <c>FixedUpdate</c> between components, so a
    /// recorder without a declared order runs part way through the objects it
    /// records: some have taken this step's value and some have not, and one row
    /// mixes the two. The symptom is subtle enough to be worth naming — channels
    /// that should agree exactly instead differ by precisely one sample, which
    /// reads as a plausible physical lag rather than as a bug. The convention
    /// this number belongs to places motion sources at -100, sensors at 0 and
    /// derived quantities at 100.
    /// </para>
    /// <para>
    /// <b>How a channel is sampled is decided per channel.</b> The default is
    /// <see cref="Sampling.Latest"/> — the value at the sample instant, a
    /// zero-order hold — and it is the only mode for string and bool channels,
    /// which have no mean and nothing to interpolate. A numeric channel can
    /// instead be averaged over the interval since the previous row, which is a
    /// real anti-alias filter when recording below the host step rate, or
    /// interpolated onto the sample instant, which aligns rows to a wanted rate
    /// without inventing information. Both call their getter on every host step
    /// rather than only on the ones that produce a row, which is why they are
    /// opted into per channel rather than switched on for the recording.
    /// </para>
    /// <para>
    /// <b>A filtered channel's getter is also called once by
    /// <see cref="BeginRecording"/></b>, to prime the filter so a recording does
    /// not open with a run of repeated values. That read is deliberately not part
    /// of the first average — it belongs to the instant before the first window —
    /// but it does happen, so a getter with a side effect sees one more call than
    /// there were host steps. Filtered getters should be pure reads; nothing here
    /// can enforce that, which is why it is written down.
    /// </para>
    /// <para>
    /// <b>Something other than Unity can pace it.</b> Set <see cref="pacing"/>
    /// to <see cref="Pacing.External"/> and <c>FixedUpdate</c> does nothing;
    /// the driver calls <see cref="Advance"/> with its own step, or
    /// <see cref="Trigger"/> to place a single row itself. A test rig that owns
    /// its own pump, a replay driven by incoming datagrams and a scene that
    /// records only on an event are the same mechanism with different callers,
    /// and none of them should need a global flag to stop this component
    /// fighting them.
    /// </para>
    /// </remarks>
    [DefaultExecutionOrder(ExecutionOrder)]
    [AddComponentMenu("BCSV/BCSV Recorder")]
    public class BcsvRecorder : MonoBehaviour
    {
        /// <summary>Runs last in a <c>FixedUpdate</c> step; nothing should sit after it.</summary>
        public const int ExecutionOrder = 1000;

        /// <summary>What decides when a row is written.</summary>
        public enum Pacing
        {
            /// <summary>The component's own <c>FixedUpdate</c>, at the physics rate.</summary>
            UnityFixedUpdate = 0,

            /// <summary>
            /// Nothing, until a caller says so. <c>FixedUpdate</c> becomes a
            /// no-op and the driver calls <see cref="Advance"/> or
            /// <see cref="Trigger"/>.
            /// </summary>
            External = 1,
        }

        /// <summary>How a channel's value for a row is derived from what the host offered.</summary>
        public enum Sampling
        {
            /// <summary>The value read at the sample instant, held until the next one.</summary>
            /// <remarks>
            /// The default, the cheapest — the getter is called only on steps
            /// that produce a row — and the only mode available to string and
            /// bool channels. Recording below the host step rate this way
            /// discards the samples in between, so anything varying faster than
            /// the recorded rate folds down into it rather than disappearing.
            /// That is often exactly what is wanted; it should be a decision
            /// rather than an accident.
            /// </remarks>
            Latest = 0,

            /// <summary>The mean of every host step since the previous row.</summary>
            /// <remarks>
            /// A boxcar filter over the decimation window, applied before the
            /// decimation rather than after it, which is what makes it an
            /// anti-alias rather than a smoothed alias. It is the mode that earns
            /// its keep when recording slower than the host runs, because every
            /// host sample is seen and contributes instead of nine in ten being
            /// discarded. Recording at or above the host rate there is at most
            /// one sample per window and it degrades to <see cref="Latest"/>.
            /// Integer columns keep the rounded mean.
            /// </remarks>
            Average = 1,

            /// <summary>
            /// Linear interpolation between the host samples either side of the
            /// sample instant.
            /// </summary>
            /// <remarks>
            /// Places a row at the instant asked for rather than at the end of
            /// the host step containing it, which is what a recording aligned to
            /// an external rate needs. It adds no information: what the host
            /// never sampled cannot be recovered by interpolating what it did,
            /// so this is rate alignment and not anti-aliasing. It also builds
            /// each row partly from a value one step old, so a channel that steps
            /// rather than varies smoothly — a state, an index, a counter — is
            /// misrepresented by it. Integer columns keep the rounded value.
            /// </remarks>
            Interpolate = 2,
        }

        // ── Configuration ───────────────────────────────────────────────────

        [Tooltip("Where the recording goes. A relative path lands in " +
                 "Application.persistentDataPath, which is the one location " +
                 "writable on every platform; pass an absolute path to put it " +
                 "anywhere else. {scene} and {timestamp} are substituted.")]
        public string outputPath = "{scene}_{timestamp}.bcsv";

        [Tooltip("Rows per second. 0 records one row per host step, at whatever " +
                 "rate that is.\n\n" +
                 "The achievable rate is quantised to the step rate, so the " +
                 "intervals between rows are uneven whenever the two do not " +
                 "divide — 300 Hz on a 1 kHz physics step alternates 4, 3, 3 ms. " +
                 "The remainder is carried, so the MEAN rate is exactly what was " +
                 "asked for even though no single interval is.\n\n" +
                 "Changing this while recording takes effect immediately.")]
        public float sampleRateHz = 0.0f;

        [Tooltip("What decides when a row is written. External makes FixedUpdate " +
                 "a no-op and leaves the pacing to whoever calls Advance() or " +
                 "Trigger().")]
        public Pacing pacing = Pacing.UnityFixedUpdate;

        [Tooltip("Begin recording in Start(). Switch off for a rig that opens the " +
                 "file itself, and call BeginRecording() when it is ready.")]
        public bool recordOnStart = true;

        [Tooltip("Row codec: delta, zoh, or flat. Set HERE and nowhere else — it " +
                 "is fixed when the writer is constructed, and the file header's " +
                 "codec flags are derived from it rather than being separately " +
                 "settable.")]
        public string rowCodec = "delta";

        [Tooltip("LZ4 compression level. 1-5 are LZ4_compress_fast, 6-9 are LZ4HC, " +
                 "so 6 is a step change rather than a notch on a dial. The cost " +
                 "lands at file close, not in the physics loop.")]
        [Range(0, 9)]
        public int compressionLevel = BcsvDefaults.CompressionLevel;

        // ── Observable state ────────────────────────────────────────────────

        /// <summary>True between a successful <see cref="BeginRecording"/> and <see cref="EndRecording"/>.</summary>
        public bool IsRecording { get; private set; }

        /// <summary>The resolved absolute path of the open recording, or null.</summary>
        public string CurrentPath { get; private set; }

        /// <summary>Rows written to the current recording.</summary>
        public long RowsWritten { get; private set; }

        /// <summary>Sample instants that produced no row because a getter failed.</summary>
        /// <remarks>
        /// Non-zero means the recording has gaps. See <see cref="WriteRow"/> for
        /// why a failed sample is dropped rather than written with whatever the
        /// previous row held.
        /// </remarks>
        public long FailedRows { get; private set; }

        /// <summary>Columns this recorder will write, in order.</summary>
        public int SubscriptionCount { get { return _channels.Count; } }

        /// <summary>The clock deciding when rows are written; null while stopped.</summary>
        public BcsvSampleClock Clock { get { return _clock; } }

        /// <summary>
        /// True when at least one channel is filtered, so every host step costs a
        /// read of it whether or not a row is written.
        /// </summary>
        public bool SamplesEveryStep
        {
            get
            {
                // Answered from the subscriptions rather than from the list built
                // at open, so it is true as soon as such a channel is subscribed
                // rather than only once recording has started.
                for (int i = 0; i < _channels.Count; i++)
                    if (_channels[i].Mode != Sampling.Latest) return true;
                return false;
            }
        }

        // ── Internals ───────────────────────────────────────────────────────

        /// <summary>One subscribed channel: how to read it, how to write it, and its filter state.</summary>
        private sealed class Channel
        {
            public string Name;
            public ColumnType Type;
            public Sampling Mode;

            /// <summary>Reads the getter and writes it straight into the row, unboxed.</summary>
            public Action<BcsvRow, int> WriteLatest;

            /// <summary>Reads the getter as a double. Null unless the channel is filtered.</summary>
            public Func<double> Read;

            /// <summary>Writes a double back into the column's own type. Null unless filtered.</summary>
            public Action<BcsvRow, int, double> WriteValue;

            public double Sum;       // Average: running total since the last row.
            public int Count;        // Average: host steps in that total.
            public double Cur;       // The value read at the end of this host step.
            public double Prev;      // The value read at the end of the previous one.
            public bool HasPrev;
        }

        private readonly List<Channel> _channels = new List<Channel>();
        private readonly HashSet<string> _taken = new HashSet<string>();

        /// <summary>The subset of channels needing a read on every host step.</summary>
        private readonly List<Channel> _filtered = new List<Channel>();

        /// <summary>This step's reads, held until every one of them has succeeded.</summary>
        private double[] _staged = new double[0];

        private BcsvLayout _layout;
        private BcsvWriter _writer;
        private BcsvSampleClock _clock;

        private float _latchedRateHz;
        private bool _reportedBadInterval;
        private bool _reportedBadRate;
        private bool _reportedGetterFailure;

        // ── Subscriptions ───────────────────────────────────────────────────
        //
        // One overload per column type rather than one generic method taking a
        // ColumnType. Two reasons, and both were defects in the sample this
        // replaces. The column type is inferred from the getter, so a Func<string>
        // can no longer be declared as a Float column and fail once per row at
        // run time. And each overload closes over a typed getter, so reading a
        // value does not box: the generic Func<object> version allocated once per
        // column per row, which at 1 kHz and thirty columns is thirty thousand
        // garbage objects a second in the physics loop.
        //
        // The bool and string overloads take no Sampling argument at all. Neither
        // type has a mean or a midpoint, so the only honest mode is Latest, and
        // leaving the argument out says so at the call site rather than accepting
        // it and quietly ignoring it.

        /// <summary>Records a bool channel. Always <see cref="Sampling.Latest"/>.</summary>
        public BcsvRecorder Track(string name, Func<bool> getter)
        {
            return Add(name, ColumnType.Bool, Sampling.Latest, getter,
                       (row, col) => row.SetBool(col, getter()), null, null);
        }

        /// <summary>Records a string channel. Always <see cref="Sampling.Latest"/>; a null reads as empty.</summary>
        public BcsvRecorder Track(string name, Func<string> getter)
        {
            return Add(name, ColumnType.String, Sampling.Latest, getter,
                       (row, col) => row.SetString(col, getter() ?? string.Empty), null, null);
        }

        /// <summary>Records a float channel.</summary>
        public BcsvRecorder Track(string name, Func<float> getter, Sampling sampling = Sampling.Latest)
        {
            return Add(name, ColumnType.Float, sampling, getter,
                       (row, col) => row.SetFloat(col, getter()),
                       () => getter(),
                       (row, col, v) => row.SetFloat(col, (float)v));
        }

        /// <summary>Records a double channel.</summary>
        public BcsvRecorder Track(string name, Func<double> getter, Sampling sampling = Sampling.Latest)
        {
            return Add(name, ColumnType.Double, sampling, getter,
                       (row, col) => row.SetDouble(col, getter()),
                       () => getter(),
                       (row, col, v) => row.SetDouble(col, v));
        }

        /// <summary>Records a signed 8-bit channel.</summary>
        public BcsvRecorder Track(string name, Func<sbyte> getter, Sampling sampling = Sampling.Latest)
        {
            return Add(name, ColumnType.Int8, sampling, getter,
                       (row, col) => row.SetInt8(col, getter()),
                       () => getter(),
                       (row, col, v) => row.SetInt8(col, (sbyte)Quantise(v, sbyte.MinValue, sbyte.MaxValue)));
        }

        /// <summary>Records a signed 16-bit channel.</summary>
        public BcsvRecorder Track(string name, Func<short> getter, Sampling sampling = Sampling.Latest)
        {
            return Add(name, ColumnType.Int16, sampling, getter,
                       (row, col) => row.SetInt16(col, getter()),
                       () => getter(),
                       (row, col, v) => row.SetInt16(col, (short)Quantise(v, short.MinValue, short.MaxValue)));
        }

        /// <summary>Records a signed 32-bit channel.</summary>
        public BcsvRecorder Track(string name, Func<int> getter, Sampling sampling = Sampling.Latest)
        {
            return Add(name, ColumnType.Int32, sampling, getter,
                       (row, col) => row.SetInt32(col, getter()),
                       () => getter(),
                       (row, col, v) => row.SetInt32(col, (int)Quantise(v, int.MinValue, int.MaxValue)));
        }

        /// <summary>Records a signed 64-bit channel. Always <see cref="Sampling.Latest"/>.</summary>
        /// <remarks>
        /// Filtering is not offered on the 64-bit integer types, on the same
        /// terms as bool and string: the filters carry values as
        /// <see cref="double"/>, which has 53 bits of mantissa and therefore
        /// cannot represent every <see cref="long"/>. An average would be exact
        /// below 2^53 and quietly wrong above it — 2^53+1 comes back as 2^53 —
        /// and the values that fail are the large ones, which is where a counter
        /// or a nanosecond timestamp lives. Record such a channel as
        /// <see cref="Sampling.Latest"/>, or as a <see cref="double"/> if a mean
        /// of it is what you actually want.
        /// </remarks>
        public BcsvRecorder Track(string name, Func<long> getter)
        {
            return Add(name, ColumnType.Int64, Sampling.Latest, getter,
                       (row, col) => row.SetInt64(col, getter()), null, null);
        }

        /// <summary>Records an unsigned 8-bit channel.</summary>
        public BcsvRecorder Track(string name, Func<byte> getter, Sampling sampling = Sampling.Latest)
        {
            return Add(name, ColumnType.UInt8, sampling, getter,
                       (row, col) => row.SetUInt8(col, getter()),
                       () => getter(),
                       (row, col, v) => row.SetUInt8(col, (byte)Quantise(v, byte.MinValue, byte.MaxValue)));
        }

        /// <summary>Records an unsigned 16-bit channel.</summary>
        public BcsvRecorder Track(string name, Func<ushort> getter, Sampling sampling = Sampling.Latest)
        {
            return Add(name, ColumnType.UInt16, sampling, getter,
                       (row, col) => row.SetUInt16(col, getter()),
                       () => getter(),
                       (row, col, v) => row.SetUInt16(col, (ushort)Quantise(v, ushort.MinValue, ushort.MaxValue)));
        }

        /// <summary>Records an unsigned 32-bit channel.</summary>
        public BcsvRecorder Track(string name, Func<uint> getter, Sampling sampling = Sampling.Latest)
        {
            return Add(name, ColumnType.UInt32, sampling, getter,
                       (row, col) => row.SetUInt32(col, getter()),
                       () => getter(),
                       (row, col, v) => row.SetUInt32(col, (uint)Quantise(v, uint.MinValue, uint.MaxValue)));
        }

        /// <summary>Records an unsigned 64-bit channel. Always <see cref="Sampling.Latest"/>.</summary>
        /// <remarks>Not filterable, for the reason given on the <see cref="long"/> overload.</remarks>
        public BcsvRecorder Track(string name, Func<ulong> getter)
        {
            return Add(name, ColumnType.UInt64, Sampling.Latest, getter,
                       (row, col) => row.SetUInt64(col, getter()), null, null);
        }

        /// <summary>Records a transform's position and rotation as seven float channels.</summary>
        public BcsvRecorder TrackTransform(Transform transform, string channelName = null,
                                           Sampling sampling = Sampling.Latest)
        {
            if (transform == null)
            {
                Debug.LogError(name + " (BcsvRecorder): TrackTransform was given no transform.", this);
                return this;
            }

            string b = string.IsNullOrEmpty(channelName) ? transform.name : channelName;
            Track(b + ".position.x", () => transform.position.x, sampling);
            Track(b + ".position.y", () => transform.position.y, sampling);
            Track(b + ".position.z", () => transform.position.z, sampling);

            // Quaternion components are averaged and interpolated one at a time
            // here, which is not a rotation average and is not slerp. Over the
            // small angles between two host steps the difference is negligible
            // and the result is not renormalised; if that matters, track the
            // rotation yourself in whatever form your analysis wants.
            Track(b + ".rotation.x", () => transform.rotation.x, sampling);
            Track(b + ".rotation.y", () => transform.rotation.y, sampling);
            Track(b + ".rotation.z", () => transform.rotation.z, sampling);
            Track(b + ".rotation.w", () => transform.rotation.w, sampling);
            return this;
        }

#if BCSV_HAS_PHYSICS
        // Compiled only where the Physics module is present. A recorder is
        // useful in a project with no physics engine at all, and this package
        // has no business requiring one for the sake of one convenience
        // method — so the dependency is declared as a versionDefine in
        // BCSV.asmdef rather than as a package dependency, and everything
        // except this method works without it.
        /// <summary>Records a rigidbody's linear and angular velocity as six float channels.</summary>
        public BcsvRecorder TrackRigidbody(Rigidbody body, string channelName = null,
                                           Sampling sampling = Sampling.Latest)
        {
            if (body == null)
            {
                Debug.LogError(name + " (BcsvRecorder): TrackRigidbody was given no rigidbody.", this);
                return this;
            }

            string b = string.IsNullOrEmpty(channelName) ? body.name : channelName;
#if UNITY_6000_0_OR_NEWER
            Track(b + ".velocity.x", () => body.linearVelocity.x, sampling);
            Track(b + ".velocity.y", () => body.linearVelocity.y, sampling);
            Track(b + ".velocity.z", () => body.linearVelocity.z, sampling);
#else
            Track(b + ".velocity.x", () => body.velocity.x, sampling);
            Track(b + ".velocity.y", () => body.velocity.y, sampling);
            Track(b + ".velocity.z", () => body.velocity.z, sampling);
#endif
            Track(b + ".angularVelocity.x", () => body.angularVelocity.x, sampling);
            Track(b + ".angularVelocity.y", () => body.angularVelocity.y, sampling);
            Track(b + ".angularVelocity.z", () => body.angularVelocity.z, sampling);
            return this;
        }
#endif

        /// <summary>Discards every subscription. Refused while recording.</summary>
        public void ClearSubscriptions()
        {
            if (RefuseWhileRecording("ClearSubscriptions")) return;
            _channels.Clear();
            _filtered.Clear();
            _taken.Clear();
        }

        private BcsvRecorder Add(string channelName, ColumnType type, Sampling sampling,
                                 object getter,
                                 Action<BcsvRow, int> writeLatest,
                                 Func<double> read,
                                 Action<BcsvRow, int, double> writeValue)
        {
            if (RefuseWhileRecording("Track")) return this;

            if (getter == null)
            {
                Debug.LogError(name + " (BcsvRecorder): channel '" + channelName +
                               "' was given no getter and was not added.", this);
                return this;
            }

            if (string.IsNullOrEmpty(channelName))
            {
                Debug.LogError(name + " (BcsvRecorder): a channel must have a name. " +
                               "Nothing was added.", this);
                return this;
            }

            // Caught here rather than at open, because here the message can name
            // the channel and the caller can see which Track call to change.
            // bcsv's layout would refuse the duplicate column without saying so,
            // leaving fewer columns than subscriptions and every later channel
            // writing into the wrong one.
            if (!_taken.Add(channelName))
            {
                Debug.LogError(name + " (BcsvRecorder): channel '" + channelName +
                               "' is already subscribed. Column names must be unique — " +
                               "pass a distinct name to Track, or a channelName to " +
                               "TrackTransform/TrackRigidbody when two objects share a name. " +
                               "The duplicate was not added.", this);
                return this;
            }

            if (sampling != Sampling.Latest && read == null)
            {
                // Unreachable through the public overloads, which do not offer the
                // argument on the types that cannot honour it. Asserted anyway,
                // because the alternative to failing here is recording a channel
                // in a mode it is not actually in.
                Debug.LogError(name + " (BcsvRecorder): channel '" + channelName + "' is " +
                               type + ", which can only be sampled as Latest. Recorded as " +
                               "Latest.", this);
                sampling = Sampling.Latest;
            }

            _channels.Add(new Channel
            {
                Name = channelName,
                Type = type,
                Mode = sampling,
                WriteLatest = writeLatest,
                Read = sampling == Sampling.Latest ? null : read,
                WriteValue = sampling == Sampling.Latest ? null : writeValue,
            });
            return this;
        }

        // A layout is fixed for the lifetime of the file it describes, so a
        // subscription added after the writer opened would have no column to go
        // in. The sample this replaces allowed it and wrote past the end of the
        // layout, which surfaced as one caught exception per row rather than as
        // the configuration error it was.
        private bool RefuseWhileRecording(string what)
        {
            if (!IsRecording) return false;
            Debug.LogError(name + " (BcsvRecorder): " + what + " was called while recording. " +
                           "A file's columns are fixed when it opens — stop the recording, " +
                           "change the subscriptions, and start another file.", this);
            return true;
        }

        // ── Lifecycle ───────────────────────────────────────────────────────

        /// <summary>Resolves the path, builds the layout and opens the file.</summary>
        /// <returns>True if the recording started.</returns>
        public bool BeginRecording()
        {
            if (IsRecording)
            {
                Debug.LogWarning(name + " (BcsvRecorder): already recording to " + CurrentPath +
                                 ". Ignored.", this);
                return true;
            }

            if (_channels.Count == 0)
            {
                Debug.LogError(name + " (BcsvRecorder): nothing is subscribed, so there is " +
                               "nothing to record. Call Track(...) before recording.", this);
                return false;
            }

            // BcsvWriter maps an unrecognised codec name onto "delta" without
            // complaining, so a typo would record correctly and silently not be
            // the codec that was asked for. Refuse it here instead.
            if (rowCodec != "delta" && rowCodec != "zoh" && rowCodec != "flat")
            {
                Debug.LogError(name + " (BcsvRecorder): rowCodec '" + rowCodec + "' is not a " +
                               "codec. Use delta, zoh, or flat. Nothing was recorded.", this);
                return false;
            }

            // Before anything is opened. Building the clock after the writer
            // meant an unusable rate threw with a native file already open, out
            // of Start(), leaving this component half-initialised around a handle
            // nothing had a reference to yet.
            BcsvSampleClock clock;
            string rateProblem;
            if (!BcsvSampleClock.TryFromRate(sampleRateHz, out clock, out rateProblem))
            {
                Debug.LogError(name + " (BcsvRecorder): sampleRateHz (" + sampleRateHz + ") " +
                               rateProblem + ". Use a finite rate in hertz, or 0 for one row " +
                               "per host step. Nothing was recorded.", this);
                return false;
            }

            _filtered.Clear();
            for (int i = 0; i < _channels.Count; i++)
            {
                Channel c = _channels[i];
                c.Sum = 0.0; c.Count = 0; c.Cur = 0.0; c.Prev = 0.0; c.HasPrev = false;
                if (c.Mode != Sampling.Latest) _filtered.Add(c);
            }

            _staged = new double[_filtered.Count];

            // Prime every filtered channel with one read, taken now rather than
            // at the end of the first host step. Without it the first step has no
            // earlier sample to interpolate from and has to repeat the value it
            // just read, so a recording opens with a short run of identical rows
            // that look like real data. It is also the earliest moment a getter
            // that cannot work can say so — better here, with no file on disk,
            // than as a dropped row once recording is under way.
            //
            // The primed value deliberately does not join the first average: it
            // belongs to the instant before the first window, not inside it.
            for (int i = 0; i < _filtered.Count; i++)
            {
                Channel c = _filtered[i];
                try
                {
                    c.Cur = c.Read();
                    c.Prev = c.Cur;
                    c.HasPrev = true;
                }
                catch (Exception ex)
                {
                    Debug.LogError(name + " (BcsvRecorder): the getter for channel '" + c.Name +
                                   "' threw while priming its filter, so nothing was recorded. " +
                                   ex.Message, this);
                    _filtered.Clear();
                    return false;
                }
            }

            string resolved = ResolvePath(outputPath);

            _layout = new BcsvLayout();
            for (int i = 0; i < _channels.Count; i++)
                _layout.AddColumn(_channels[i].Name, _channels[i].Type);

            // Every write below addresses a column by its subscription index, so
            // the two must be the same length. bcsv reports a rejected column by
            // not adding it rather than by failing, and a short layout would send
            // every later channel into the wrong column.
            if (_layout.ColumnCount != _channels.Count)
            {
                Debug.LogError(name + " (BcsvRecorder): the layout came out with " +
                               _layout.ColumnCount + " columns for " + _channels.Count +
                               " channels, so at least one was rejected. Nothing was " +
                               "recorded.", this);
                _layout.Dispose(); _layout = null;
                return false;
            }

            try
            {
                _writer = new BcsvWriter(_layout, rowCodec);
            }
            catch (Exception ex)
            {
                Debug.LogError(name + " (BcsvRecorder): could not create the writer — " +
                               ex.Message, this);
                _layout.Dispose(); _layout = null;
                return false;
            }

            if (!_writer.TryOpen(resolved, overwrite: true, compression: compressionLevel))
            {
                Debug.LogError(name + " (BcsvRecorder): could not open " + resolved + " — " +
                               _writer.ErrorMessage, this);
                _writer.Dispose(); _writer = null;
                _layout.Dispose(); _layout = null;
                return false;
            }

            _latchedRateHz = sampleRateHz;
            _clock = clock;
            CurrentPath = resolved;
            RowsWritten = 0;
            FailedRows = 0;
            _reportedBadInterval = false;
            _reportedBadRate = false;
            _reportedGetterFailure = false;
            IsRecording = true;

            Debug.Log(name + " (BcsvRecorder): recording " + _channels.Count + " channels to " +
                      resolved + (_filtered.Count > 0
                          ? " (" + _filtered.Count + " filtered, so every host step reads them)"
                          : string.Empty), this);
            return true;
        }

        /// <summary>Closes the file. Safe to call when not recording.</summary>
        public void EndRecording()
        {
            if (!IsRecording && _writer == null && _layout == null) return;

            string path = CurrentPath;
            long rows = RowsWritten;
            long failed = FailedRows;

            IsRecording = false;
            if (_writer != null) { _writer.Dispose(); _writer = null; }
            if (_layout != null) { _layout.Dispose(); _layout = null; }
            _clock = null;
            _filtered.Clear();
            CurrentPath = null;

            if (failed > 0)
                Debug.LogError(name + " (BcsvRecorder): wrote " + rows + " rows to " + path +
                               " and dropped " + failed + " because a getter failed. The " +
                               "recording has gaps.", this);
            else
                Debug.Log(name + " (BcsvRecorder): wrote " + rows + " rows to " + path, this);
        }

        /// <summary>Pushes buffered rows to disk. Costly — not for the physics loop.</summary>
        /// <remarks>
        /// The batch codec compresses a whole packet at once, which is what keeps
        /// <see cref="Trigger"/> cheap; flushing forfeits that for everything
        /// buffered so far. Worth doing at a checkpoint, not per row.
        /// </remarks>
        public void Flush()
        {
            if (IsRecording && _writer != null) _writer.Flush();
        }

        // ── Pacing ──────────────────────────────────────────────────────────

        /// <summary>
        /// Advances the sample clock by one host step and writes whatever rows
        /// fall inside it.
        /// </summary>
        /// <param name="dt">The host interval, in seconds.</param>
        /// <returns>Rows written by this call: 0, 1, or several.</returns>
        /// <remarks>
        /// Reports a bad interval loudly and once, then does nothing with it. An
        /// interval that is quietly absorbed becomes an unexplainable row count
        /// weeks later with nothing pointing at the cause; an exception thrown
        /// once per physics step is its own kind of failure, which is why this
        /// layer reports rather than rethrows.
        /// </remarks>
        public int Advance(float dt)
        {
            if (!IsRecording) return 0;

            if (!(dt >= 0.0f) || float.IsInfinity(dt))
            {
                if (!_reportedBadInterval)
                {
                    _reportedBadInterval = true;
                    Debug.LogError(name + " (BcsvRecorder): Advance(" + dt + ") — the interval " +
                                   "must be positive and finite. Nothing was recorded. A " +
                                   "timeline that jumped is a new recording, not an advance.",
                                   this);
                }
                return 0;
            }

            // Honour a rate changed from the inspector or by a script mid-run.
            // Silently ignoring a field somebody explicitly set is the failure
            // mode this component exists to avoid.
            if (sampleRateHz != _latchedRateHz)
            {
                // Latch either way, so an unusable value is judged once rather
                // than on every step; the clock in use is kept unchanged.
                _latchedRateHz = sampleRateHz;

                BcsvSampleClock changed;
                string rateProblem;
                if (BcsvSampleClock.TryFromRate(sampleRateHz, out changed, out rateProblem))
                {
                    _clock = changed;
                }
                else if (!_reportedBadRate)
                {
                    _reportedBadRate = true;
                    Debug.LogError(name + " (BcsvRecorder): sampleRateHz was changed to " +
                                   sampleRateHz + ", which " + rateProblem + ". Still recording " +
                                   "at the previous rate.", this);
                }
            }

            // Filtered channels are read here, on every step, because that is
            // what a filter is: an average over samples that were taken, and an
            // interpolation between samples that bracket the instant. Reading
            // them only where a row is due would average one sample and
            // interpolate between a value and itself.
            bool sampled = SampleFilteredChannels();

            int edges = _clock.Advance(dt);

            if (!sampled)
            {
                // No filter state moved, so there is nothing to commit: this step
                // simply produced no sample. An interpolated channel will then
                // span two host steps on the next successful one, which is
                // unavoidable — the sample that would have sat between them was
                // never taken.
                FailedRows += edges;
                return 0;
            }

            int written = 0;
            for (int e = 0; e < edges; e++)
                if (WriteRow(_clock.EdgeFraction(e, dt))) written++;

            CommitFilterStep();
            return written;
        }

        /// <summary>Writes exactly one row now, whatever the clock thinks.</summary>
        /// <returns>True if a row was written.</returns>
        /// <remarks>
        /// For a caller whose sample instants are events rather than a rate — a
        /// controller step, an arriving datagram, a state change. It does not
        /// touch the clock: a trigger <i>is</i> the sample instant, so there is
        /// no window to average over and no pair to interpolate between, and a
        /// filtered channel contributes its value at this moment exactly as
        /// <see cref="Sampling.Latest"/> would.
        /// </remarks>
        public bool Trigger()
        {
            if (!IsRecording) return false;

            if (!SampleFilteredChannels())
            {
                FailedRows++;
                return false;
            }

            bool ok = WriteRow(1.0f);
            CommitFilterStep();
            return ok;
        }

        // ── Sampling ────────────────────────────────────────────────────────

        /// <summary>Reads every filtered channel once for this host step.</summary>
        /// <returns>False if a getter threw, in which case no filter state moved.</returns>
        /// <remarks>
        /// Two passes, and the split is the point. Reading and accumulating in
        /// one loop means a getter that throws part way along leaves the channels
        /// before it holding this step's sample and the ones after it holding the
        /// previous step's — so their averaging windows and their interpolation
        /// pairs quietly diverge, which is a corruption of exactly the data the
        /// filters exist to get right. Nothing moves until every read has
        /// succeeded.
        /// </remarks>
        private bool SampleFilteredChannels()
        {
            for (int i = 0; i < _filtered.Count; i++)
            {
                try
                {
                    _staged[i] = _filtered[i].Read();
                }
                catch (Exception ex)
                {
                    ReportGetterFailure(_filtered[i].Name, ex);
                    return false;
                }
            }

            for (int i = 0; i < _filtered.Count; i++)
            {
                Channel c = _filtered[i];
                c.Cur = _staged[i];
                c.Sum += c.Cur;
                c.Count++;
            }
            return true;
        }

        /// <summary>Ends the host step: this step's value becomes the previous one.</summary>
        private void CommitFilterStep()
        {
            for (int i = 0; i < _filtered.Count; i++)
            {
                Channel c = _filtered[i];
                c.Prev = c.Cur;
                c.HasPrev = true;
            }
        }

        /// <summary>Reads every channel and commits one row.</summary>
        /// <param name="fraction">
        /// Where inside this host step the sample instant fell, from
        /// <see cref="BcsvSampleClock.EdgeFraction"/>. Only interpolated channels
        /// use it.
        /// </param>
        /// <remarks>
        /// A getter that throws drops the whole row rather than committing a
        /// partial one. bcsv reuses the row buffer between writes, so a column
        /// left unset keeps the previous row's value — and under the delta and
        /// zero-order-hold codecs an unchanged value is exactly what a genuinely
        /// constant channel looks like, so a stale sample is invisible in the
        /// file. A missing row is visible in whatever the caller tracks as time.
        /// Prefer the gap you can see.
        /// </remarks>
        private bool WriteRow(float fraction)
        {
            BcsvRow row = _writer.Row;

            for (int i = 0; i < _channels.Count; i++)
            {
                Channel c = _channels[i];
                try
                {
                    switch (c.Mode)
                    {
                        case Sampling.Average:
                            // Count is zero only when several sample instants fell
                            // inside one host step, so this window holds no sample
                            // of its own. Hold the last value rather than invent
                            // one: at that rate there is nothing to average.
                            //
                            // The accumulator is NOT cleared here. Clearing as we
                            // walk the columns means a getter that throws further
                            // along leaves the channels before it reset and the
                            // ones after it not, so the next row averages a
                            // different span of time per channel — a quiet
                            // corruption of exactly the data the filter exists to
                            // get right. Cleared below, once the row is committed.
                            c.WriteValue(row, i, c.Count > 0 ? c.Sum / c.Count : c.Cur);
                            break;

                        case Sampling.Interpolate:
                            c.WriteValue(row, i, c.HasPrev
                                ? c.Prev + fraction * (c.Cur - c.Prev)
                                : c.Cur);
                            break;

                        default:
                            c.WriteLatest(row, i);
                            break;
                    }
                }
                catch (Exception ex)
                {
                    FailedRows++;
                    ReportGetterFailure(c.Name, ex);
                    return false;
                }
            }

            _writer.WriteRow();
            RowsWritten++;

            // Only now: every getter ran and the row reached the writer, so every
            // averaging window closed at the same instant.
            for (int f = 0; f < _filtered.Count; f++)
            {
                Channel c = _filtered[f];
                if (c.Mode != Sampling.Average) continue;
                c.Sum = 0.0;
                c.Count = 0;
            }

            return true;
        }

        private void ReportGetterFailure(string channelName, Exception ex)
        {
            if (_reportedGetterFailure) return;
            _reportedGetterFailure = true;
            Debug.LogError(name + " (BcsvRecorder): the getter for channel '" + channelName +
                           "' threw, so this row was dropped. Reported once; FailedRows " +
                           "counts the rest. " + ex.Message, this);
        }

        // ── Unity hooks ─────────────────────────────────────────────────────

        private void Start()
        {
            if (recordOnStart) BeginRecording();
        }

        private void FixedUpdate()
        {
            if (pacing != Pacing.UnityFixedUpdate) return;
            Advance(Time.fixedDeltaTime);
        }

        private void OnDestroy()
        {
            EndRecording();
        }

        // ── Helpers ─────────────────────────────────────────────────────────


        /// <summary>Rounds a filtered value onto an integer column without wrapping.</summary>
        /// <remarks>
        /// <para>
        /// A mean or an interpolated value is generally not an integer, so
        /// something has to happen to the fraction and rounding is the least
        /// surprising thing.
        /// </para>
        /// <para>
        /// The clamp is not decoration. An out-of-range float-to-integer cast
        /// saturates on CoreCLR but is undefined in C++, which is what IL2CPP
        /// compiles to — so on the runtime this component actually ships to, an
        /// unclamped conversion could land as a plausible value of the wrong
        /// sign rather than as anything anyone would notice. Clamping first makes
        /// the result the same everywhere.
        /// </para>
        /// <para>
        /// Every range this is called with is exactly representable as a double:
        /// the 64-bit integer types are not filterable precisely because theirs
        /// are not.
        /// </para>
        /// </remarks>
        private static double Quantise(double v, double lo, double hi)
        {
            if (double.IsNaN(v)) return 0.0;
            v = Math.Round(v, MidpointRounding.AwayFromZero);
            return v < lo ? lo : (v > hi ? hi : v);
        }

        /// <summary>Substitutes {scene} and {timestamp} and roots a relative path.</summary>
        /// <remarks>
        /// The timestamp is local time in a form that sorts: a recording is
        /// identified by when it was taken, and a directory listing should put
        /// them in that order without anybody having to sort it.
        /// </remarks>
        public static string ResolvePath(string template)
        {
            string named = (template ?? string.Empty)
                .Replace("{scene}", SceneManager.GetActiveScene().name)
                .Replace("{timestamp}", DateTime.Now.ToString("yyyyMMdd-HHmmss"));

            if (string.IsNullOrEmpty(named)) named = "recording.bcsv";

            return System.IO.Path.IsPathRooted(named)
                ? named
                : System.IO.Path.Combine(Application.persistentDataPath, named);
        }
    }
}
