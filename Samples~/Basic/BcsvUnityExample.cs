/*
 * Copyright (c) 2025-2026 Tobias Weber <weber.tobias.md@gmail.com>
 * 
 * This file is part of the BCSV library.
 * 
 * Licensed under the MIT License. See LICENSE file in the project root 
 * for full license information.
 */

using System;
using UnityEngine;
using BCSV;

namespace BCSV.Examples
{
    /// <summary>
    /// Example demonstrating the updated BCSV Unity C# bindings
    /// Shows cross-platform filename handling and new Row API features
    /// </summary>
    public class BcsvUnityExample : MonoBehaviour
    {
        void Start()
        {
            Debug.Log("BCSV Unity Example - Updated API Demo");
            Debug.Log("====================================");

            // Test the new Row API features
            TestRowApiFeatures();
            
            // Test cross-platform filename handling
            TestCrossPlatformFilenames();
        }

        void TestRowApiFeatures()
        {
            Debug.Log("\n--- Row API Features ---");

            string testFile = System.IO.Path.Combine(Application.persistentDataPath, "row_api_test.bcsv");

            // Create a layout with fluent chaining
            using (var layout = new BcsvLayout())
            {
                layout.AddColumn("id", ColumnType.Int32)
                      .AddColumn("name", ColumnType.String)
                      .AddColumn("value", ColumnType.Double);

                Debug.Log($"Layout: {layout}");

                // Write rows — Row is a lightweight struct handle, not IDisposable
                using (var writer = new BcsvWriter(layout))
                {
                    writer.Open(testFile, overwrite: true);

                    var row = writer.Row;
                    row.SetInt32(0, 42);
                    row.SetString(1, "Hello");
                    row.SetDouble(2, 3.14159);
                    writer.WriteRow();

                    row.SetInt32(0, 99);
                    row.SetString(1, "World");
                    row.SetDouble(2, 2.71828);
                    writer.WriteRow();

                    Debug.Log($"Wrote {writer.RowCount} rows");
                }

                // Read rows back using foreach
                using (var reader = new BcsvReader())
                {
                    reader.Open(testFile);
                    Debug.Log($"Row count: {reader.RowCount}");

                    foreach (var row in reader)
                    {
                        Debug.Log($"  id={row.GetInt32(0)}, name={row.GetString(1)}, value={row.GetDouble(2)}");
                    }
                }

                // Test version API
                Debug.Log($"BCSV version: {BcsvVersion.Version} ({BcsvVersion.Major}.{BcsvVersion.Minor}.{BcsvVersion.Patch})");
            }
        }

        void TestCrossPlatformFilenames()
        {
            Debug.Log("\n--- Cross-Platform Filename Handling ---");

            string testFile = System.IO.Path.Combine(Application.persistentDataPath, "unity_test.bcsv");
            Debug.Log($"Test file path: {testFile}");

            // Create a simple layout
            using (var layout = new BcsvLayout())
            {
                layout.AddColumn("id", ColumnType.Int32)
                      .AddColumn("message", ColumnType.String);

                // Test writer filename handling
                using (var writer = new BcsvWriter(layout))
                {
                    writer.Open(testFile, overwrite: true);

                    string writerFilename = writer.Filename;
                    Debug.Log($"Writer filename: {writerFilename}");

                    var row = writer.Row;
                    row.SetInt32(0, 123);
                    row.SetString(1, "Hello Unity!");
                    writer.WriteRow();
                    writer.Close();
                }

                // Test reader filename handling
                using (var reader = new BcsvReader())
                {
                    reader.Open(testFile);

                    string readerFilename = reader.Filename;
                    Debug.Log($"Reader filename: {readerFilename}");

                    if (reader.ReadNext())
                    {
                        var row = reader.Row;
                        Debug.Log($"Read data - ID: {row.GetInt32(0)}, Message: {row.GetString(1)}");
                    }
                    reader.Close();
                }
            }

            Debug.Log("✅ Cross-platform filename handling test completed!");
        }
    }
}