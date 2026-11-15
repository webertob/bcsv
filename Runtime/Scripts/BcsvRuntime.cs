// Copyright (c) 2025-2026 Tobias Weber. Licensed under the MIT License.
using System;
using System.Collections.Generic;
using UnityEngine;
using UnityEngine.Scripting;

namespace BCSV
{
    /// <summary>
    /// Process-lifecycle glue between Unity and the native library.
    /// </summary>
    /// <remarks>
    /// <para>
    /// A built player that quits with a recording still open used to abort the
    /// exit teardown (double-free at process exit, rows lost) or die silently
    /// without ever writing the file footer, because nothing ran <c>Close()</c>.
    /// On application quit this class now ends every live
    /// <see cref="BcsvRecorder"/> recording in reverse start order and then
    /// hands the native library <see cref="Shutdown"/>, which closes every
    /// still-open writer — including plain <see cref="BcsvWriter"/>s nobody
    /// disposed — so every file gets a valid footer before the process goes.
    /// </para>
    /// <para>
    /// After <see cref="Shutdown"/> every native handle from before the call is
    /// dead; later <c>Dispose()</c> calls on old wrappers are safe logged
    /// no-ops. Do not call <see cref="Shutdown"/> mid-session unless you also
    /// drop every live wrapper — the library is done only when the process is.
    /// </para>
    /// </remarks>
    [Preserve]
    public static class BcsvRuntime
    {
        // Weak: a recorder destroyed without EndRecording must not be kept
        // alive (or revived for a quit callback) by this registry.
        private static readonly List<WeakReference<BcsvRecorder>> Recordings =
            new List<WeakReference<BcsvRecorder>>();
        private static bool _quitHooked;

        /// <summary>
        /// Runs on every domain (re)load and on play-mode start, including with
        /// domain reloads disabled by Enter Play Mode options.
        /// </summary>
        [RuntimeInitializeOnLoadMethod(RuntimeInitializeLoadType.SubsystemRegistration)]
        private static void Initialize()
        {
            // With domain reloads off, statics survive between sessions; a
            // stale hook or stale recorder list must not fire twice.
            if (_quitHooked) Application.quitting -= OnQuitting;
            Recordings.Clear();
            Application.quitting += OnQuitting;
            _quitHooked = true;
        }

        internal static void Register(BcsvRecorder recorder)
        {
            // Idempotent: BeginRecording on an already-registered recorder
            // must not leave two entries (they would both be ended on quit).
            for (int i = 0; i < Recordings.Count; i++)
                if (Recordings[i].TryGetTarget(out var r) && ReferenceEquals(r, recorder)) return;
            Recordings.Add(new WeakReference<BcsvRecorder>(recorder));
        }

        internal static void Unregister(BcsvRecorder recorder)
        {
            for (int i = Recordings.Count - 1; i >= 0; i--)
            {
                if (!Recordings[i].TryGetTarget(out var r) || ReferenceEquals(r, recorder))
                    Recordings.RemoveAt(i);   // also prunes dead entries
            }
        }

        private static void OnQuitting() => Shutdown();

        /// <summary>
        /// Ends every live recorder recording and tears the native library
        /// down: open writers are closed (valid footers on disk), all handles
        /// freed. Idempotent; the quit hook calls it, you may call it from a
        /// controlled shutdown path of your own.
        /// </summary>
        public static void Shutdown()
        {
            // Reverse order: the last recorder started is usually the one
            // depending on the earlier ones (clocks, paced sources).
            for (int i = Recordings.Count - 1; i >= 0; i--)
            {
                if (Recordings[i].TryGetTarget(out var rec) && rec != null && rec.IsRecording)
                {
                    try { rec.EndRecording(); }
                    catch (Exception ex)
                    {
                        Debug.LogError("BcsvRuntime: EndRecording during shutdown failed: " + ex);
                    }
                }
            }
            Recordings.Clear();

            // Native catch-all: closes writers nobody owned up to closing, and
            // makes every later destroy a safe, logged no-op.
            NativeMethods.bcsv_shutdown();
        }
    }
}
