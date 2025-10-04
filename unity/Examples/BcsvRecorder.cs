/*
 * Copyright (c) 2025 Tobias Weber <weber.tobias.md@gmail.com>
 * 
 * This file is part of the BCSV library.
 * 
 * Licensed under the MIT License. See LICENSE file in the project root 
 * for full license information.
 */

using BCSV;
using UnityEngine;
using System.Collections.Generic;
using System;

// Modern C# approach using Func delegates as "pointers"
[System.Serializable]
public class DataSubscription
{
    [SerializeField] public string displayName;
    [SerializeField] public ColumnType columnType;
    
    // Function pointer to get the value
    [System.NonSerialized] public Func<object> getValue;
    
    public DataSubscription(string name, ColumnType type, Func<object> getter)
    {
        displayName = name;
        columnType = type;
        getValue = getter;
    }
    
    public bool IsValid => getValue != null;
}

// Fluent builder for creating subscriptions
public class SubscriptionBuilder
{
    private List<DataSubscription> subscriptions = new List<DataSubscription>();
    
    public SubscriptionBuilder Track<T>(string name, Func<T> getter, ColumnType columnType = ColumnType.Float)
    {
        subscriptions.Add(new DataSubscription(name, columnType, () => getter()));
        return this;
    }
    
    public SubscriptionBuilder TrackTransform(Transform transform, string objectName = null)
    {
        string baseName = objectName ?? transform.name;
        subscriptions.Add(new DataSubscription($"{baseName}.position.x", ColumnType.Float, () => transform.position.x));
        subscriptions.Add(new DataSubscription($"{baseName}.position.y", ColumnType.Float, () => transform.position.y));
        subscriptions.Add(new DataSubscription($"{baseName}.position.z", ColumnType.Float, () => transform.position.z));
        subscriptions.Add(new DataSubscription($"{baseName}.rotation.x", ColumnType.Float, () => transform.rotation.x));
        subscriptions.Add(new DataSubscription($"{baseName}.rotation.y", ColumnType.Float, () => transform.rotation.y));
        subscriptions.Add(new DataSubscription($"{baseName}.rotation.z", ColumnType.Float, () => transform.rotation.z));
        subscriptions.Add(new DataSubscription($"{baseName}.rotation.w", ColumnType.Float, () => transform.rotation.w));
        return this;
    }
    
    public SubscriptionBuilder TrackRigidbody(Rigidbody rb, string objectName = null)
    {
        if (rb == null) return this;
        
        string baseName = objectName ?? rb.name;
        subscriptions.Add(new DataSubscription($"{baseName}.velocity.x", ColumnType.Float, () => rb.linearVelocity.x));
        subscriptions.Add(new DataSubscription($"{baseName}.velocity.y", ColumnType.Float, () => rb.linearVelocity.y));
        subscriptions.Add(new DataSubscription($"{baseName}.velocity.z", ColumnType.Float, () => rb.linearVelocity.z));
        subscriptions.Add(new DataSubscription($"{baseName}.mass", ColumnType.Float, () => rb.mass));
        return this;
    }
    
    public SubscriptionBuilder TrackExcenter(Excenter exc, string objectName = null)
    {
        if (exc == null) return this;
        
        string baseName = objectName ?? exc.name;
        subscriptions.Add(new DataSubscription($"{baseName}.CurrentPhase", ColumnType.Float, () => exc.CurrentPhase));
        subscriptions.Add(new DataSubscription($"{baseName}.CurrentFrequency", ColumnType.Float, () => exc.CurrentFrequency));
        subscriptions.Add(new DataSubscription($"{baseName}.CurrentAmplitude", ColumnType.String, () => exc.CurrentAmplitude.ToString()));
        subscriptions.Add(new DataSubscription($"{baseName}.CurrentForce", ColumnType.String, () => exc.CurrentForce.ToString()));
        return this;
    }
    
    public List<DataSubscription> Build() => subscriptions;
}

public class BcsvRecorder : MonoBehaviour
{
    [SerializeField] private string filePath = "c:/ws/test_{timestamp}.bcsv";
    [SerializeField] private float timerInterval = 0.1f;
    [SerializeField] private bool running = true;
    
    private float timerCounter = 0.0f;
    private List<DataSubscription> subscriptions = new List<DataSubscription>();
    
    private BcsvLayout layout;
    private BcsvWriter writer;

    void MakeSubscriptions()
    {
        // Modern C# approach using lambdas as "pointers"
        var builder = new SubscriptionBuilder();
        
        // Method 1: Individual tracking with lambdas
        Transform cubeTransform0 = GameObject.Find("Cube_0")?.transform;
        if (cubeTransform0 != null)
        {
            builder.TrackTransform(cubeTransform0, "Cube_0");
        }

        Transform cubeTransform1 = GameObject.Find("Cube_1")?.transform;
        if (cubeTransform1 != null)
        {
            builder.TrackTransform(cubeTransform1, "Cube_1");
        }

        Transform cubeTransform2 = GameObject.Find("Cube_2")?.transform;
        if (cubeTransform2 != null)
        {
            builder.TrackTransform(cubeTransform2, "Cube_2");
        }

        // Method 2: Track specific properties with type-safe lambdas
        Rigidbody cubeRb0 = GameObject.Find("Cube_0")?.GetComponent<Rigidbody>();
        if (cubeRb0 != null)
        {
            builder.TrackRigidbody(cubeRb0, "Cube_0");
        }
        
        // Method 3: Custom tracking with full lambda control
        Excenter exc = GameObject.Find("Cube_0")?.GetComponent<Excenter>();
        if (exc != null)
        {
            builder.TrackExcenter(exc, "Cube_0");
        }
        
        //// Method 4: Direct lambda subscriptions (most flexible)
        //GameObject player = GameObject.Find("Player");
        //if (player != null)
        //{
        //    builder
        //        .Track("Player.Health", () => player.GetComponent<Health>()?.currentHealth ?? 0, ColumnType.Float)
        //        .Track("Player.Speed", () => player.GetComponent<PlayerController>()?.speed ?? 0f, ColumnType.Float)
        //        .Track("Player.IsGrounded", () => player.GetComponent<PlayerController>()?.isGrounded ?? false, ColumnType.Bool);
        //}
        
        //// Method 5: Batch tracking for multiple objects
        //GameObject[] enemies = GameObject.FindGameObjectsWithTag("Enemy");
        //for (int i = 0; i < enemies.Length; i++)
        //{
        //    var enemy = enemies[i];
        //    var enemyTransform = enemy.transform;
        //    var enemyAI = enemy.GetComponent<EnemyAI>();
            
        //    builder
        //        .Track($"Enemy{i}.position.x", () => enemyTransform.position.x, ColumnType.Float)
        //        .Track($"Enemy{i}.position.y", () => enemyTransform.position.y, ColumnType.Float)
        //        .Track($"Enemy{i}.position.z", () => enemyTransform.position.z, ColumnType.Float)
        //        .Track($"Enemy{i}.state", () => enemyAI?.currentState.ToString() ?? "None", ColumnType.String);
        //}
        
        subscriptions = builder.Build();
        Debug.Log($"Created {subscriptions.Count} function pointer subscriptions");
    }

    void Start()
    {
        MakeSubscriptions();
        CreateLayout();
        
        string timestamp = System.DateTime.Now.ToString("yyyyMMdd_HHmmss");
        string finalPath = filePath.Replace("{timestamp}", timestamp);
        
        writer = new BcsvWriter(layout);
        if (writer.Open(finalPath, true, 1, 64, FileFlags.ZOH))
        {
            Debug.Log($"Started recording to: {finalPath}");
            timerCounter = timerInterval;
        }
        else
        {
            Debug.LogError($"Failed to open BCSV file for writing: {finalPath}");
            running = false;
        }
    }

    private void CreateLayout()
    {
        layout = new BcsvLayout();
        layout.AddColumn("Timestamp", ColumnType.Float);
        
        foreach (var subscription in subscriptions)
        {
            if (subscription.IsValid)
            {
                layout.AddColumn(subscription.displayName, subscription.columnType);
            }
        }
        
        Debug.Log($"Created layout with {layout.ColumnCount} columns");
    }

    private void WriteRow()
    {
        if (writer == null || !writer.IsOpen) return;
        
        var row = writer.Row;
        if (row == null) return;
        
        int columnIndex = 0;
        row.SetFloat(columnIndex++, Time.time);
        
        foreach (var subscription in subscriptions)
        {
            if (subscription.IsValid)
            {
                try
                {
                    object value = subscription.getValue(); // Call the function pointer!
                    if (value != null)
                    {
                        SetRowValue(row, columnIndex, value, subscription.columnType);
                    }
                }
                catch (Exception ex)
                {
                    Debug.LogWarning($"Failed to get value for {subscription.displayName}: {ex.Message}");
                }
                columnIndex++;
            }
        }
        
        writer.Next();
    }

    private void SetRowValue(BcsvRowRef row, int columnIndex, object value, ColumnType columnType)
    {
        try
        {
            switch (columnType)
            {
                case ColumnType.Bool:
                    row.SetBool(columnIndex, Convert.ToBoolean(value));
                    break;
                case ColumnType.Float:
                    row.SetFloat(columnIndex, Convert.ToSingle(value));
                    break;
                case ColumnType.Double:
                    row.SetDouble(columnIndex, Convert.ToDouble(value));
                    break;
                case ColumnType.Int32:
                    row.SetInt32(columnIndex, Convert.ToInt32(value));
                    break;
                case ColumnType.String:
                    row.SetString(columnIndex, value?.ToString());
                    break;
                default:
                    row.SetString(columnIndex, value?.ToString());
                    break;
            }
        }
        catch (Exception ex)
        {
            Debug.LogWarning($"Failed to set value at column {columnIndex}: {ex.Message}");
        }
    }

    private void FixedUpdate()
    {
        if (running)
        {
            timerCounter -= Time.fixedDeltaTime;
            if (timerCounter <= 0.0f)
            {
                timerCounter += timerInterval;
                WriteRow();
            }
        }
    }

    void OnDestroy()
    {
        writer?.Dispose();
        layout?.Dispose();
    }
    
    // Runtime subscription methods
    public void AddSubscription<T>(string name, Func<T> getter, ColumnType columnType = ColumnType.Float)
    {
        subscriptions.Add(new DataSubscription(name, columnType, () => getter()));
    }
}