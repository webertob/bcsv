/*
 * Copyright (c) 2025-2026 Tobias Weber <weber.tobias.md@gmail.com>
 *
 * This file is part of the BCSV library.
 *
 * Licensed under the MIT License. See LICENSE file in the project root
 * for full license information.
 */

using BCSV;
using UnityEngine;

/// <summary>
/// Configures the supported <see cref="BcsvRecorder"/> for the demo scene.
/// </summary>
/// <remarks>
/// <para>
/// The recorder itself lives in the package — subscriptions, sample rate,
/// execution order, pacing and file lifetime are all its job, and it is
/// supported code that receives fixes. What stays here is the part that is only
/// ever true of one scene: <i>which</i> objects to record. Copy this file, point
/// it at your own objects, and leave the recorder where it is.
/// </para>
/// <para>
/// Put this component on the same GameObject as the recorder. It runs in
/// <c>Awake</c>, before the recorder's <c>Start</c> opens the file, because a
/// file's columns are fixed once it is open.
/// </para>
/// </remarks>
[RequireComponent(typeof(BcsvRecorder))]
public class BcsvRecorderDemo : MonoBehaviour
{
    [Tooltip("Objects to record. Left empty, the demo looks for Cube_0..2.")]
    public Transform[] targets;

    private void Awake()
    {
        var recorder = GetComponent<BcsvRecorder>();

        // The recorder writes no time column, because what a recording calls
        // time is a property of the experiment: a simulation clock, a host
        // timestamp or a step counter, in whatever unit and width suits it.
        // Track it like any other channel. Double, not float — single precision
        // runs out of millisecond resolution after about two hours of scene
        // time, which is long enough not to notice during a test and short
        // enough to matter during a run.
        recorder.Track("t", () => Time.fixedTimeAsDouble);
        recorder.Track("step", () => (long)(Time.fixedTimeAsDouble / Time.fixedDeltaTime));

        foreach (var target in Resolve())
        {
            // Position is smooth, and this recording runs slower than physics
            // does, so average it: every physics step contributes instead of
            // nine in ten being discarded. That is what stops motion faster than
            // 50 Hz folding down into the recording as if it belonged there.
            recorder.TrackTransform(target, sampling: BcsvRecorder.Sampling.Average);

#if BCSV_HAS_PHYSICS
            // The column type comes from the getter, so there is no way to
            // declare a channel one type and feed it another.
            var body = target.GetComponent<Rigidbody>();
            if (body != null)
            {
                recorder.TrackRigidbody(body, sampling: BcsvRecorder.Sampling.Average);
                recorder.Track(target.name + ".mass", () => body.mass);

                // Left as Latest deliberately. A bool has no mean, and the
                // overload does not offer the argument — a sleeping flag is a
                // state, and the honest answer is what it was at the instant the
                // row was written.
                recorder.Track(target.name + ".sleeping", () => body.IsSleeping());
            }
#endif
        }

        // 0 records one row per physics step. Anything else is decimated with
        // the remainder carried, so the mean rate is exact even where the
        // individual intervals cannot be.
        recorder.sampleRateHz = 100.0f;
    }

    private Transform[] Resolve()
    {
        if (targets != null && targets.Length > 0) return targets;

        var found = new System.Collections.Generic.List<Transform>();
        for (int i = 0; i < 3; i++)
        {
            var go = GameObject.Find("Cube_" + i);
            if (go != null) found.Add(go.transform);
        }

        if (found.Count == 0)
            Debug.LogWarning("BcsvRecorderDemo: no targets assigned and no Cube_N in the " +
                             "scene, so there is nothing to record.", this);

        return found.ToArray();
    }
}
