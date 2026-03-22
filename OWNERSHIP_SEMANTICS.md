# BCSV Unity Row Ownership Semantics

## Overview

The BCSV Unity C# bindings now implement a 4-class row hierarchy that provides compile-time type safety for ownership semantics. This design prevents common programming errors and makes the API more explicit about value vs reference semantics.

## Class Hierarchy

### 1. `BcsvRowBase` (Abstract Base Class)
- **Purpose**: Defines the common interface for all row types
- **Contains**: All getter methods, virtual setter methods, common row operations
- **Use When**: Never instantiated directly; provides polymorphic base

### 2. `BcsvRow` (Owning Value Type)
- **Purpose**: Creates and owns its native handle with value semantics
- **Lifecycle**: Must be disposed to free native resources
- **Creation**: 
  - `BcsvRow.Create(layout)` - creates new row
  - `BcsvRow.Clone(source)` - clones from another row
- **Use When**: You need an independent row that you manage the lifetime of

```csharp
// Creating an owned row
using (var layout = new BcsvLayout())
{
    layout.AddColumn("id", ColumnType.Int32);
    using (var row = BcsvRow.Create(layout))
    {
        row.SetInt32(0, 42);
        
        // Clone creates another owned row
        using (var clone = BcsvRow.Clone(row))
        {
            // Both rows are independent
        }
    } // row disposed here
}
```

### 3. `BcsvRowRef` (Mutable Reference Type)
- **Purpose**: Non-owning mutable reference to an existing row
- **Lifecycle**: Doesn't manage native handle lifecycle
- **Creation**: Only created internally by `BcsvWriter.Row`
- **Use When**: Writer scenarios where you need to modify row data

```csharp
using (var writer = new BcsvWriter(layout))
{
    writer.Open("test.bcsv");
    
    // Get mutable reference to writer's row
    BcsvRowRef row = writer.Row;
    row.SetInt32(0, 123);
    row.SetString(1, "Hello");
    
    writer.Next(); // Advance to next row
    // row reference is still valid for the next row
}
```

### 4. `BcsvRowRefConst` (Immutable Reference Type)
- **Purpose**: Non-owning immutable reference to an existing row
- **Lifecycle**: Doesn't manage native handle lifecycle  
- **Creation**: Only created internally by `BcsvReader.Row`
- **Safety**: All setter methods throw `InvalidOperationException`
- **Use When**: Reader scenarios where data should not be modified

```csharp
using (var reader = new BcsvReader())
{
    reader.Open("test.bcsv");
    
    while (reader.Next())
    {
        // Get immutable reference to reader's row
        BcsvRowRefConst row = reader.Row;
        int id = row.GetInt32(0);
        string name = row.GetString(1);
        
        // This would throw InvalidOperationException:
        // row.SetInt32(0, 999); // Compile-time error prevention!
    }
}
```

## Key Benefits

### 1. **Compile-Time Safety**
- Prevents accidental modification of read-only data
- Clear ownership semantics prevent resource leaks
- Type system enforces correct usage patterns

### 2. **Performance**
- Reference types avoid unnecessary copying
- Zero-copy access to native row data
- Efficient memory management

### 3. **API Clarity**
- `BcsvRow` = "I own this data and am responsible for cleanup"
- `BcsvRowRef` = "I can modify this data but don't own it"
- `BcsvRowRefConst` = "I can only read this data"

## Migration Guide

### Before (Old API)
```csharp
// Old: All rows were BcsvRow, unclear ownership
BcsvRow readerRow = reader.Row; // Actually a reference!
BcsvRow writerRow = writer.Row; // Actually a reference!
BcsvRow ownedRow = new BcsvRow(layout); // Actually owned!
```

### After (New API)
```csharp
// New: Clear ownership semantics
BcsvRowRefConst readerRow = reader.Row; // Clearly immutable reference
BcsvRowRef writerRow = writer.Row; // Clearly mutable reference  
BcsvRow ownedRow = BcsvRow.Create(layout); // Clearly owned value
```

## Common Patterns

### Reading Data
```csharp
using (var reader = new BcsvReader())
{
    reader.Open("data.bcsv");
    reader.ReadAll((row, index) => {
        // row is BcsvRowRefConst - immutable reference
        Console.WriteLine($"Row {index}: {row.GetString(0)}");
    });
}
```

### Writing Data
```csharp
using (var writer = new BcsvWriter(layout))
{
    writer.Open("output.bcsv");
    writer.WriteRows(100, (row, index) => {
        // row is BcsvRowRef - mutable reference
        row.SetInt32(0, index);
        row.SetString(1, $"Item {index}");
    });
}
```

### Data Processing
```csharp
using (var sourceRow = BcsvRow.Create(layout))
{
    // Fill sourceRow with data...
    sourceRow.SetInt32(0, 42);
    
    // Clone for independent processing
    using (var processedRow = BcsvRow.Clone(sourceRow))
    {
        processedRow.SetInt32(0, processedRow.GetInt32(0) * 2);
        // Both rows exist independently
    }
}
```

## Error Prevention

The new type system prevents common errors:

```csharp
// Prevents: Modifying read-only data
BcsvRowRefConst readerRow = reader.Row;
// readerRow.SetInt32(0, 123); // ❌ Throws InvalidOperationException

// Prevents: Resource leaks with clear ownership
using (var ownedRow = BcsvRow.Create(layout)) 
{
    // ✅ Automatically disposed
}

// Prevents: Confusion about reference vs value semantics  
BcsvRowRef writerRef = writer.Row; // ✅ Clearly a reference
BcsvRow ownedCopy = BcsvRow.Clone(writerRef); // ✅ Clearly creates owned copy
```

This design provides both safety and performance while making the API's intent crystal clear.