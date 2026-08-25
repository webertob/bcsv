// Copyright (c) 2025-2026 Tobias Weber. Licensed under the MIT License.
using System;
using System.Collections.Generic;
using System.Text;
using UnityEngine;
using UnityEngine.SceneManagement;

namespace BCSV
{
    /// <summary>
    /// Plays a BCSV recording back into a running scene, one row per sample
    /// instant.
    /// </summary>
    /// <remarks>
    /// <para>
    /// The mirror of <see cref="BcsvRecorder"/>, and deliberately its mirror in
    /// shape as well as in direction: the same <see cref="BcsvSampleClock"/>
    /// decides when a row is due, the same <see cref="Advance"/> and
    /// <see cref="Trigger"/> let something other than Unity decide instead, and
    /// a channel is a name plus a delegate. Where the recorder pulls values out
    /// of the scene with a getter, this pushes them back in with a setter.
    /// </para>
    /// <para>
    /// <b>It does not read the recording's own timestamps.</b> This is a
    /// decision, not an omission. A recording's time channel is whatever its
    /// author chose to call time — seconds or milliseconds, absolute or
    /// relative, float or double, or a step counter, under any column name — so
    /// there is nothing for this component to look for. It plays rows at the
    /// rate <see cref="playbackRateHz"/> asks for and counts them; the
    /// recording's own notion of time arrives as an ordinary bound channel, for
    /// the caller to interpret. <b>The consequence worth stating plainly: if the
    /// rate here does not match the rate the file was recorded at, playback runs
    /// fast or slow and nothing detects it.</b>
    /// </para>
    /// <para>
    /// <b>Execution order is -1000, and nothing should sit before it.</b> A
    /// player is a source: everything reading the values it publishes has to run
    /// after it, or it spends a step working on the previous row. That is the
    /// same failure the recorder's +1000 exists to prevent, seen from the other
    /// end — the two bracket a step between them.
    /// </para>
    /// <para>
    /// Rows are addressed by index rather than read as a stream, which costs
    /// about 60 ns a row more and buys one code path for playback, seeking and
    /// looping alike. It uses the file's packet index; a file written without
    /// one has it rebuilt when this component opens it.
    /// </para>
    /// </remarks>
    [DefaultExecutionOrder(ExecutionOrder)]
    [AddComponentMenu("BCSV/BCSV Player")]
    public class BcsvPlayer : MonoBehaviour
    {
        /// <summary>Runs first in a step; nothing should sit before it.</summary>
        /// <remarks>The mirror of <see cref="BcsvRecorder.ExecutionOrder"/>.</remarks>
        public const int ExecutionOrder = -1000;

        /// <summary>What decides when the next row is presented.</summary>
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

        // ── Configuration ───────────────────────────────────────────────────

        [Tooltip("The recording to play. A relative path is resolved against " +
                 "Application.persistentDataPath. {scene} is substituted.\n\n" +
                 "There is deliberately no {timestamp}: it cannot match a file " +
                 "that already exists.")]
        public string inputPath = "recording.bcsv";

        [Tooltip("Rows per second. 0 presents one row per host step.\n\n" +
                 "This component does not read the recording's own timestamps — " +
                 "a recording's idea of time is a column like any other, and " +
                 "this cannot know which one. If this rate does not match the " +
                 "rate the file was recorded at, playback runs fast or slow and " +
                 "nothing will tell you.\n\n" +
                 "Changing this while playing takes effect immediately.")]
        public float playbackRateHz = 0.0f;

        [Tooltip("What decides when a row is presented. External makes " +
                 "FixedUpdate a no-op and leaves the pacing to whoever calls " +
                 "Advance() or Trigger().")]
        public Pacing pacing = Pacing.UnityFixedUpdate;

        [Tooltip("Open the file and start playing in Start(). Switch off for a " +
                 "rig that opens it itself, and call BeginPlayback().")]
        public bool playOnStart = true;

        [Tooltip("Return to the first row instead of stopping at the end.")]
        public bool loop = false;

        // ── Observable state ────────────────────────────────────────────────

        /// <summary>True while the file is open and rows are still being presented.</summary>
        /// <remarks>
        /// Goes false as soon as the last row of a non-looping recording has been
        /// presented — on that row, not on the edge after it. The file stays
        /// open, so <see cref="Seek"/> and <see cref="Play"/> can resume.
        /// </remarks>
        public bool IsPlaying { get; private set; }

        /// <summary>True between <see cref="BeginPlayback"/> and <see cref="EndPlayback"/>.</summary>
        public bool IsOpen { get { return _reader != null && _reader.IsOpen; } }

        /// <summary>The resolved absolute path of the open recording, or null.</summary>
        public string CurrentPath { get; private set; }

        /// <summary>Index of the row currently presented, or -1 before the first.</summary>
        public long RowIndex { get; private set; }

        /// <summary>Rows in the open recording.</summary>
        public long RowCount { get; private set; }

        /// <summary>Rows presented since <see cref="BeginPlayback"/>, laps included.</summary>
        public long RowsPresented { get; private set; }

        /// <summary>Rows that could not be presented because a setter threw.</summary>
        public long FailedRows { get; private set; }

        /// <summary>True once the last row has been presented and looping is off.</summary>
        public bool AtEnd { get { return RowCount > 0 && RowIndex >= RowCount - 1; } }

        /// <summary>The clock deciding when rows are due; null while closed.</summary>
        public BcsvSampleClock Clock { get { return _clock; } }

        /// <summary>The layout of the open recording, or null.</summary>
        public BcsvLayout Layout { get { return IsOpen ? _reader.Layout : null; } }

        /// <summary>
        /// The row currently presented, for reading columns no binding covers.
        /// </summary>
        /// <remarks>
        /// Valid only while <see cref="IsOpen"/> and after the first row has been
        /// presented. Bindings are the tidier way to consume a recording; this is
        /// here because a caller who wants six columns out of four hundred should
        /// not have to bind them.
        /// </remarks>
        public BcsvRow Row { get { return _reader.Row; } }

        /// <summary>Raised once when the last row of a non-looping recording has been presented.</summary>
        public event Action Completed;

        // ── Internals ───────────────────────────────────────────────────────

        /// <summary>One bound channel: which column, what type, and where it goes.</summary>
        private sealed class Binding
        {
            public string Column;
            public ColumnType Type;
            public Action<BcsvRow, int> Apply;
            public int Index = -1;    // resolved against the file's layout at open
        }

        private readonly List<Binding> _bindings = new List<Binding>();

        private BcsvReader _reader;
        private BcsvSampleClock _clock;

        private float _latchedRateHz;
        private bool _reportedBadInterval;
        private bool _reportedBadRate;
        private bool _reportedSetterFailure;
        private bool _reportedCompletion;

        // ── Bindings ────────────────────────────────────────────────────────
        //
        // One method per column type, named after the matching BcsvRow getter.
        // The type is fixed by the delegate, so a channel cannot be bound as one
        // type and read as another, and delivering a value does not box.
        //
        // NAMED RATHER THAN OVERLOADED, and the asymmetry with the recorder's
        // Track is forced by the language rather than chosen. Track takes a
        // getter, whose RETURN type the compiler infers from the lambda body, so
        // one overloaded name resolves cleanly. A setter's parameter type cannot
        // be inferred the same way: given Bind("x", v => foo = v), every
        // Action<T> overload is a candidate and the call is ambiguous. Overloads
        // here would mean writing Bind("x", (float v) => ...) at every call site,
        // and meeting CS0121 before learning that.
        //
        // Unlike the recorder, binding the same column twice is allowed. There a
        // duplicate name meant a file with two columns of one name, which cannot
        // exist; here it means one channel feeding two consumers, which is an
        // ordinary thing to want.

        /// <summary>Delivers a bool column.</summary>
        public BcsvPlayer BindBool(string column, Action<bool> setter)
        { return Add(column, ColumnType.Bool, setter, (row, i) => setter(row.GetBool(i))); }

        /// <summary>Delivers a string column.</summary>
        public BcsvPlayer BindString(string column, Action<string> setter)
        { return Add(column, ColumnType.String, setter, (row, i) => setter(row.GetString(i))); }

        /// <summary>Delivers a float column.</summary>
        public BcsvPlayer BindFloat(string column, Action<float> setter)
        { return Add(column, ColumnType.Float, setter, (row, i) => setter(row.GetFloat(i))); }

        /// <summary>Delivers a double column.</summary>
        public BcsvPlayer BindDouble(string column, Action<double> setter)
        { return Add(column, ColumnType.Double, setter, (row, i) => setter(row.GetDouble(i))); }

        /// <summary>Delivers a signed 8-bit column.</summary>
        public BcsvPlayer BindInt8(string column, Action<sbyte> setter)
        { return Add(column, ColumnType.Int8, setter, (row, i) => setter(row.GetInt8(i))); }

        /// <summary>Delivers a signed 16-bit column.</summary>
        public BcsvPlayer BindInt16(string column, Action<short> setter)
        { return Add(column, ColumnType.Int16, setter, (row, i) => setter(row.GetInt16(i))); }

        /// <summary>Delivers a signed 32-bit column.</summary>
        public BcsvPlayer BindInt32(string column, Action<int> setter)
        { return Add(column, ColumnType.Int32, setter, (row, i) => setter(row.GetInt32(i))); }

        /// <summary>Delivers a signed 64-bit column.</summary>
        public BcsvPlayer BindInt64(string column, Action<long> setter)
        { return Add(column, ColumnType.Int64, setter, (row, i) => setter(row.GetInt64(i))); }

        /// <summary>Delivers an unsigned 8-bit column.</summary>
        public BcsvPlayer BindUInt8(string column, Action<byte> setter)
        { return Add(column, ColumnType.UInt8, setter, (row, i) => setter(row.GetUInt8(i))); }

        /// <summary>Delivers an unsigned 16-bit column.</summary>
        public BcsvPlayer BindUInt16(string column, Action<ushort> setter)
        { return Add(column, ColumnType.UInt16, setter, (row, i) => setter(row.GetUInt16(i))); }

        /// <summary>Delivers an unsigned 32-bit column.</summary>
        public BcsvPlayer BindUInt32(string column, Action<uint> setter)
        { return Add(column, ColumnType.UInt32, setter, (row, i) => setter(row.GetUInt32(i))); }

        /// <summary>Delivers an unsigned 64-bit column.</summary>
        public BcsvPlayer BindUInt64(string column, Action<ulong> setter)
        { return Add(column, ColumnType.UInt64, setter, (row, i) => setter(row.GetUInt64(i))); }

        /// <summary>Discards every binding. Refused while a file is open.</summary>
        public void ClearBindings()
        {
            if (RefuseWhileOpen("ClearBindings")) return;
            _bindings.Clear();
        }

        private BcsvPlayer Add(string column, ColumnType type, object setter,
                               Action<BcsvRow, int> apply)
        {
            if (RefuseWhileOpen("Bind*")) return this;

            if (setter == null)
            {
                Debug.LogError(name + " (BcsvPlayer): column '" + column +
                               "' was given no setter and was not bound.", this);
                return this;
            }

            if (string.IsNullOrEmpty(column))
            {
                Debug.LogError(name + " (BcsvPlayer): a binding must name a column. " +
                               "Nothing was bound.", this);
                return this;
            }

            _bindings.Add(new Binding { Column = column, Type = type, Apply = apply });
            return this;
        }

        // A binding is resolved against the layout of the file that is open, so
        // one added afterwards would have no column index. Rather than resolve it
        // late and hope, this refuses — the same reasoning as the recorder's, from
        // the other side of the file.
        private bool RefuseWhileOpen(string what)
        {
            if (!IsOpen) return false;
            Debug.LogError(name + " (BcsvPlayer): " + what + " was called while a recording " +
                           "is open. Bindings are resolved against the file's columns when it " +
                           "opens — call EndPlayback() first.", this);
            return true;
        }

        // ── Lifecycle ───────────────────────────────────────────────────────

        /// <summary>Opens the recording, resolves every binding and starts playing.</summary>
        /// <returns>True if playback started.</returns>
        /// <remarks>
        /// Every binding is checked against the file's real layout before a row
        /// is presented, and <b>all</b> the problems are reported rather than
        /// only the first: a caller with thirty bindings and three typos should
        /// see three, not discover them one run at a time.
        /// </remarks>
        public bool BeginPlayback()
        {
            if (IsOpen)
            {
                Debug.LogWarning(name + " (BcsvPlayer): already playing " + CurrentPath +
                                 ". Ignored.", this);
                return true;
            }

            // Before the file is opened, so an unusable rate cannot throw with a
            // native reader already in hand.
            BcsvSampleClock clock;
            string rateProblem;
            if (!BcsvSampleClock.TryFromRate(playbackRateHz, out clock, out rateProblem))
            {
                Debug.LogError(name + " (BcsvPlayer): playbackRateHz (" + playbackRateHz + ") " +
                               rateProblem + ". Use a finite rate in hertz, or 0 for one row " +
                               "per host step. Nothing was played.", this);
                return false;
            }

            string resolved = ResolvePath(inputPath);

            _reader = new BcsvReader();

            // rebuildFooter: a recording written with NO_FILE_INDEX has no packet
            // index, and addressing rows by index needs one. Rebuilding costs a
            // scan at open and nothing afterwards.
            if (!_reader.TryOpen(resolved, rebuildFooter: true))
            {
                Debug.LogError(name + " (BcsvPlayer): could not open " + resolved + " — " +
                               _reader.ErrorMessage, this);
                _reader.Dispose(); _reader = null;
                return false;
            }

            if (!ResolveBindings(resolved))
            {
                _reader.Dispose(); _reader = null;
                return false;
            }

            RowCount = _reader.RowCount;
            if (RowCount <= 0)
            {
                Debug.LogError(name + " (BcsvPlayer): " + resolved + " holds no rows.", this);
                _reader.Dispose(); _reader = null;
                return false;
            }

            _latchedRateHz = playbackRateHz;
            _clock = clock;
            CurrentPath = resolved;
            RowIndex = -1;
            RowsPresented = 0;
            FailedRows = 0;
            _reportedBadInterval = false;
            _reportedBadRate = false;
            _reportedSetterFailure = false;
            _reportedCompletion = false;
            IsPlaying = true;

            Debug.Log(name + " (BcsvPlayer): playing " + RowCount + " rows from " + resolved +
                      " through " + _bindings.Count + " bindings", this);
            return true;
        }

        /// <summary>Matches every binding to a real column, or explains why it cannot.</summary>
        private bool ResolveBindings(string path)
        {
            BcsvLayout layout = _reader.Layout;

            // Name -> index, built once. A recording can be a thousand columns
            // wide and a caller can bind a hundred of them; scanning the layout
            // per binding would be quadratic for no reason.
            var byName = new Dictionary<string, int>(layout.ColumnCount);
            for (int i = 0; i < layout.ColumnCount; i++)
                byName[layout[i].Name] = i;

            StringBuilder problems = null;
            for (int b = 0; b < _bindings.Count; b++)
            {
                Binding binding = _bindings[b];
                binding.Index = -1;

                int index;
                if (!byName.TryGetValue(binding.Column, out index))
                {
                    problems = problems ?? new StringBuilder();
                    problems.Append("\n  '").Append(binding.Column)
                            .Append("' is not a column in this recording");
                    continue;
                }

                ColumnType actual = layout[index].Type;
                if (actual != binding.Type)
                {
                    problems = problems ?? new StringBuilder();
                    problems.Append("\n  '").Append(binding.Column).Append("' is ")
                            .Append(actual).Append(" in the file but was bound as ")
                            .Append(binding.Type);
                    continue;
                }

                binding.Index = index;
            }

            if (problems == null) return true;

            Debug.LogError(name + " (BcsvPlayer): " + path + " does not match the bindings, so " +
                           "nothing was played:" + problems +
                           "\nThe recording's columns are: " + Describe(layout), this);
            return false;
        }

        private static string Describe(BcsvLayout layout)
        {
            var sb = new StringBuilder();
            int shown = Math.Min(layout.ColumnCount, 24);
            for (int i = 0; i < shown; i++)
            {
                if (i > 0) sb.Append(", ");
                sb.Append(layout[i].Name).Append(':').Append(layout[i].Type);
            }
            if (layout.ColumnCount > shown)
                sb.Append(", ... (").Append(layout.ColumnCount - shown).Append(" more)");
            return sb.ToString();
        }

        /// <summary>Closes the recording. Safe to call when nothing is open.</summary>
        public void EndPlayback()
        {
            if (_reader == null) return;

            string path = CurrentPath;
            long presented = RowsPresented;
            long failed = FailedRows;

            IsPlaying = false;
            _reader.Dispose(); _reader = null;
            _clock = null;
            CurrentPath = null;
            RowIndex = -1;

            if (failed > 0)
                Debug.LogError(name + " (BcsvPlayer): presented " + presented + " rows from " +
                               path + " and dropped " + failed + " because a setter threw.", this);
            else
                Debug.Log(name + " (BcsvPlayer): presented " + presented + " rows from " + path,
                          this);
        }

        /// <summary>Resumes a paused or finished recording.</summary>
        /// <remarks>
        /// At the end of a non-looping recording this does nothing on its own —
        /// there is no next row. <see cref="Seek"/> somewhere first.
        /// </remarks>
        public void Play()
        {
            if (!IsOpen)
            {
                Debug.LogError(name + " (BcsvPlayer): nothing is open to play.", this);
                return;
            }
            IsPlaying = true;
        }

        /// <summary>Stops presenting rows without closing the recording.</summary>
        public void Pause()
        {
            IsPlaying = false;
        }

        /// <summary>
        /// Moves to <paramref name="index"/> and presents that row immediately.
        /// </summary>
        /// <returns>True if the row was presented.</returns>
        /// <remarks>
        /// A discontinuity, not an advance: it does not touch the clock's phase
        /// and it produces exactly one row wherever it lands. Seeking a finished
        /// recording leaves it paused — call <see cref="Play"/> to run on from
        /// there, which keeps "where am I" and "am I running" separate decisions.
        /// Seeking <i>to</i> the last row of a non-looping recording finishes it,
        /// because that row has then been presented and
        /// <see cref="Completed"/> promises to fire when it has.
        /// </remarks>
        public bool Seek(long index)
        {
            if (!IsOpen)
            {
                Debug.LogError(name + " (BcsvPlayer): nothing is open to seek in.", this);
                return false;
            }

            if (index < 0 || index >= RowCount)
            {
                Debug.LogError(name + " (BcsvPlayer): row " + index + " is outside this " +
                               "recording's 0.." + (RowCount - 1) + ".", this);
                return false;
            }

            _reportedCompletion = false;
            if (!Present(index)) return false;

            // Landing on the last row of a non-looping recording is the last row
            // having been presented, which is the whole of the completion
            // contract. Going through Present alone would leave IsPlaying true
            // and Completed unraised until something stepped again — the same
            // one-edge lag that Step was fixed for, reachable by a different
            // route.
            if (!loop && index >= RowCount - 1) Finish();
            return true;
        }

        // ── Pacing ──────────────────────────────────────────────────────────

        /// <summary>
        /// Advances the clock by one host step and presents whatever rows fall
        /// inside it.
        /// </summary>
        /// <param name="dt">The host interval, in seconds.</param>
        /// <returns>Rows presented by this call: 0, 1, or several.</returns>
        /// <remarks>
        /// Where several rows fall inside one step, <b>every one of them is
        /// presented</b> rather than only the last. A setter that simply assigns
        /// will show only the final value, which is the same thing either way;
        /// one that accumulates would silently lose rows if this skipped ahead,
        /// and this component cannot tell the two apart.
        /// </remarks>
        public int Advance(float dt)
        {
            if (!IsPlaying || !IsOpen) return 0;

            if (!(dt >= 0.0f) || float.IsInfinity(dt))
            {
                if (!_reportedBadInterval)
                {
                    _reportedBadInterval = true;
                    Debug.LogError(name + " (BcsvPlayer): Advance(" + dt + ") — the interval " +
                                   "must be positive and finite. Nothing was presented. A " +
                                   "timeline that jumped is a Seek, not an advance.", this);
                }
                return 0;
            }

            if (playbackRateHz != _latchedRateHz)
            {
                _latchedRateHz = playbackRateHz;

                BcsvSampleClock changed;
                string rateProblem;
                if (BcsvSampleClock.TryFromRate(playbackRateHz, out changed, out rateProblem))
                {
                    _clock = changed;
                }
                else if (!_reportedBadRate)
                {
                    _reportedBadRate = true;
                    Debug.LogError(name + " (BcsvPlayer): playbackRateHz was changed to " +
                                   playbackRateHz + ", which " + rateProblem + ". Still playing " +
                                   "at the previous rate.", this);
                }
            }

            int due = _clock.Advance(dt);
            int presented = 0;
            for (int i = 0; i < due; i++)
            {
                if (!Step()) break;
                presented++;
                if (!IsPlaying) break;   // the row just presented was the last one
            }
            return presented;
        }

        /// <summary>Presents the next row now, whatever the clock thinks.</summary>
        /// <returns>True if a row was presented.</returns>
        /// <remarks>
        /// For a caller whose sample instants are events rather than a rate. It
        /// does not touch the clock: a trigger <i>is</i> the instant.
        /// </remarks>
        public bool Trigger()
        {
            if (!IsPlaying || !IsOpen) return false;
            return Step();
        }

        /// <summary>Moves to the next row, wrapping or finishing at the end.</summary>
        /// <remarks>
        /// A non-looping recording finishes as soon as its last row has been
        /// presented, not on the following edge. Testing "is there a next row"
        /// first would have been simpler and is what the documented contract
        /// says this does not do: it would leave <see cref="IsPlaying"/> true and
        /// <see cref="Completed"/> unraised for one whole sample interval after
        /// the recording had visibly ended — a second at 1 Hz, and forever for a
        /// single-row file that is never stepped again.
        /// </remarks>
        private bool Step()
        {
            long next = RowIndex + 1;

            if (next >= RowCount)
            {
                if (!loop) { Finish(); return false; }
                next = 0;
            }

            if (!Present(next)) return false;

            if (!loop && next >= RowCount - 1) Finish();
            return true;
        }

        /// <summary>Stops playback and raises <see cref="Completed"/>, once.</summary>
        private void Finish()
        {
            IsPlaying = false;
            if (_reportedCompletion) return;
            _reportedCompletion = true;

            var handler = Completed;
            if (handler == null) return;

            // A handler that throws must not take the player's own state with
            // it: it is the subscriber's bug, and the recording is finished
            // either way.
            try { handler(); }
            catch (Exception ex)
            {
                Debug.LogError(name + " (BcsvPlayer): a Completed handler threw. " + ex, this);
            }
        }

        /// <summary>Reads one row and hands it to every binding.</summary>
        /// <remarks>
        /// A setter that throws stops this row where it stands: the bindings
        /// before it have run and the ones after it have not, so the scene holds
        /// a half-applied row. That is worse than a skipped row and is why it is
        /// counted and reported — but it cannot be prevented from here, because
        /// the values have already left this component.
        /// </remarks>
        private bool Present(long index)
        {
            if (!_reader.Read(index))
            {
                FailedRows++;
                Debug.LogError(name + " (BcsvPlayer): could not read row " + index + " — " +
                               _reader.ErrorMessage, this);
                IsPlaying = false;
                return false;
            }

            RowIndex = index;
            BcsvRow row = _reader.Row;

            for (int b = 0; b < _bindings.Count; b++)
            {
                Binding binding = _bindings[b];
                try
                {
                    binding.Apply(row, binding.Index);
                }
                catch (Exception ex)
                {
                    FailedRows++;
                    if (!_reportedSetterFailure)
                    {
                        _reportedSetterFailure = true;
                        Debug.LogError(name + " (BcsvPlayer): the setter for column '" +
                                       binding.Column + "' threw on row " + index + ", so that " +
                                       "row was only partly applied. Reported once; FailedRows " +
                                       "counts the rest. " + ex.Message, this);
                    }
                    return false;
                }
            }

            RowsPresented++;
            return true;
        }

        // ── Unity hooks ─────────────────────────────────────────────────────

        private void Start()
        {
            if (playOnStart) BeginPlayback();
        }

        private void FixedUpdate()
        {
            if (pacing != Pacing.UnityFixedUpdate) return;
            Advance(Time.fixedDeltaTime);
        }

        private void OnDestroy()
        {
            EndPlayback();
        }

        // ── Helpers ─────────────────────────────────────────────────────────


        /// <summary>Substitutes {scene} and roots a relative path.</summary>
        /// <remarks>
        /// <see cref="BcsvRecorder"/>'s <c>{timestamp}</c> is deliberately absent:
        /// it names a file at the moment one is created, and cannot name one that
        /// already exists.
        /// </remarks>
        public static string ResolvePath(string template)
        {
            string named = (template ?? string.Empty)
                .Replace("{scene}", SceneManager.GetActiveScene().name);

            if (string.IsNullOrEmpty(named)) named = "recording.bcsv";

            return System.IO.Path.IsPathRooted(named)
                ? named
                : System.IO.Path.Combine(Application.persistentDataPath, named);
        }
    }
}
