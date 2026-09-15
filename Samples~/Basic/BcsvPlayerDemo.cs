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
/// Replays what <see cref="BcsvRecorderDemo"/> recorded, back onto a transform.
/// </summary>
/// <remarks>
/// <para>
/// The pairing is the point: the recorder pulled these channels out of the scene
/// with getters, and the player pushes the same channels back in with setters,
/// through the same column names. Nothing in either component knows what a cube
/// is.
/// </para>
/// <para>
/// Set <c>inputPath</c> on the <see cref="BcsvPlayer"/> to a file the recorder
/// wrote — it logs the full path when it closes one — and put both components on
/// the same GameObject.
/// </para>
/// </remarks>
[RequireComponent(typeof(BcsvPlayer))]
public class BcsvPlayerDemo : MonoBehaviour
{
    [Tooltip("What the recording drives. Defaults to this object's own transform.")]
    public Transform target;

    [Tooltip("Column prefix the recording used — the name of the object that was recorded.")]
    public string channelPrefix = "Cube_0";

    // Position and rotation arrive one component at a time, because that is how
    // they were recorded: a BCSV column holds one scalar. Accumulate them and
    // apply once per row, rather than writing a Transform seven times.
    private Vector3 _position;
    private Quaternion _rotation = Quaternion.identity;

    private void Awake()
    {
        var player = GetComponent<BcsvPlayer>();
        if (target == null) target = transform;

        string p = channelPrefix;
        player.BindFloat(p + ".position.x", v => _position.x = v)
              .BindFloat(p + ".position.y", v => _position.y = v)
              .BindFloat(p + ".position.z", v => _position.z = v)
              .BindFloat(p + ".rotation.x", v => _rotation.x = v)
              .BindFloat(p + ".rotation.y", v => _rotation.y = v)
              .BindFloat(p + ".rotation.z", v => _rotation.z = v)
              .BindFloat(p + ".rotation.w", v => _rotation.w = v);

        // The recording's own time channel is a column like any other. The
        // player does not read it — it cannot know which column means time — so
        // it arrives here for whatever the scene wants to do with it.
        player.BindDouble("t", v => RecordedTime = v);

        player.Completed += () => Debug.Log(
            $"{name}: replayed {player.RowsPresented} rows, last recorded time {RecordedTime:F3}s",
            this);
    }

    /// <summary>The time the current row was recorded at, in the recording's own units.</summary>
    public double RecordedTime { get; private set; }

    // Runs after the player, whose execution order is -1000, so the values below
    // are always this step's row rather than the previous one.
    private void FixedUpdate()
    {
        target.SetPositionAndRotation(_position, _rotation);
    }
}
