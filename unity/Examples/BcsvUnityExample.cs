/*
 * Copyright (c) 2025 Tobias Weber <weber.tobias.md@gmail.com>
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

            // Create a layout
            using (var layout = new BcsvLayout())
            {
                layout.AddColumn("id", ColumnType.Int32);
                layout.AddColumn("name", ColumnType.String);
                layout.AddColumn("value", ColumnType.Double);

                // Test row creation and lifecycle
                using (var row = BcsvRow.Create(layout))
                {
                    Debug.Log("✓ Created row with layout");

                    // Change tracking is compile-time only (Row<> does not enable it)
                    Debug.Log($"Tracks changes: {row.TracksChanges}");
                    Debug.Log($"Has changes initially: {row.HasAnyChanges}");

                    // Set some values
                    row.SetInt32(0, 42);
                    row.SetString(1, "Test");
                    row.SetDouble(2, 3.14159);
                    
                    Debug.Log($"Has changes after setting values: {row.HasAnyChanges}");

                    // Test cloning
                    using (var clonedRow = BcsvRow.Clone(row))
                    {
                        Debug.Log("✓ Cloned row successfully");
                        Debug.Log($"Cloned row ID: {clonedRow.GetInt32(0)}");
                        Debug.Log($"Cloned row name: {clonedRow.GetString(1)}");
                        Debug.Log($"Cloned row value: {clonedRow.GetDouble(2)}");

                        // Test assignment
                        clonedRow.SetInt32(0, 99);
                        clonedRow.SetString(1, "Modified");
                        
                        row.Assign(clonedRow);
                        Debug.Log($"After assignment - ID: {row.GetInt32(0)}, Name: {row.GetString(1)}");
                    }

                    // Test clear
                    row.Clear();
                    Debug.Log($"After clear - ID: {row.GetInt32(0)}, Name: '{row.GetString(1)}'");
                }
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
                layout.AddColumn("id", ColumnType.Int32);
                layout.AddColumn("message", ColumnType.String);

                // Test writer filename handling
                using (var writer = new BcsvWriter(layout))
                {
                    bool opened = writer.Open(testFile, overwrite: true);
                    Debug.Log($"Writer opened: {opened}");
                    
                    if (opened)
                    {
                        // This now works cross-platform (wchar_t* on Windows, char* on POSIX)
                        string writerFilename = writer.Filename;
                        Debug.Log($"Writer filename: {writerFilename}");
                        Debug.Log($"Filename contains test file: {writerFilename?.Contains("unity_test.bcsv")}");

                        // Write a sample row
                        var row = writer.Row;
                        row.SetInt32(0, 123);
                        row.SetString(1, "Hello Unity!");
                        writer.Next();
                        
                        writer.Close();
                    }
                }

                // Test reader filename handling
                using (var reader = new BcsvReader())
                {
                    bool opened = reader.Open(testFile);
                    Debug.Log($"Reader opened: {opened}");
                    
                    if (opened)
                    {
                        // This now works cross-platform (wchar_t* on Windows, char* on POSIX)
                        string readerFilename = reader.Filename;
                        Debug.Log($"Reader filename: {readerFilename}");
                        Debug.Log($"Filename contains test file: {readerFilename?.Contains("unity_test.bcsv")}");

                        // Read the data back
                        if (reader.Next())
                        {
                            var row = reader.Row;
                            int id = row.GetInt32(0);
                            string message = row.GetString(1);
                            Debug.Log($"Read data - ID: {id}, Message: {message}");
                        }
                        
                        reader.Close();
                    }
                }
            }

            Debug.Log("\n✅ Cross-platform filename handling test completed!");
            Debug.Log("Key benefits:");
            Debug.Log("- Windows: Native Unicode support (wchar_t*)");
            Debug.Log("- POSIX: Native char support");
            Debug.Log("- Unity: Unified string interface across all platforms");
            Debug.Log("- Zero-copy filename access from native library");
        }
    }
}