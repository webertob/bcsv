#!/usr/bin/env pwsh

Write-Host "LZ4 Comprehensive Performance Comparison" -ForegroundColor Cyan
Write-Host "=======================================" -ForegroundColor Cyan
Write-Host "Frame API vs Raw Streaming vs Independent Blocks" -ForegroundColor Cyan
Write-Host ""

# Function to extract metrics from output
function Extract-Metrics {
    param([string]$Output)
    
    $metrics = @{
        CompressionTime = 0
        DecompressionTime = 0
        RandomAccessTime = 0
        Ratio = 0.0
        SpaceSaved = 0.0
        CompressedSize = 0
        BlockCount = 0
    }
    
    if ($Output -match "Compression time: (\d+) ms") { 
        $metrics.CompressionTime = [int]$matches[1] 
    }
    if ($Output -match "(?:Decompression|Full decompression) time: (\d+) ms") { 
        $metrics.DecompressionTime = [int]$matches[1] 
    }
    if ($Output -match "Random access time: (\d+) ms") { 
        $metrics.RandomAccessTime = [int]$matches[1] 
    }
    if ($Output -match "Compression ratio: ([\d.]+):1") { 
        $metrics.Ratio = [double]$matches[1] 
    }
    if ($Output -match "Space saved: ([\d.]+)%") { 
        $metrics.SpaceSaved = [double]$matches[1] 
    }
    if ($Output -match "Compressed file size: (\d+) bytes") { 
        $metrics.CompressedSize = [int]$matches[1] 
    }
    if ($Output -match "Blocks created: (\d+)") { 
        $metrics.BlockCount = [int]$matches[1] 
    }
    
    return $metrics
}

Write-Host "Running Frame API example..." -ForegroundColor Yellow
cd c:\ws\bcsv
$frameOutput = & ".\build\bin\Release\lz4_streaming_file_example.exe" | Out-String
$frameMetrics = Extract-Metrics $frameOutput

Write-Host "Running Raw Streaming API example..." -ForegroundColor Yellow
$rawOutput = & ".\build\bin\Release\lz4_raw_streaming_example.exe" | Out-String
$rawMetrics = Extract-Metrics $rawOutput

Write-Host "Running Independent Blocks example..." -ForegroundColor Yellow
$independentOutput = & ".\build\bin\Release\lz4_independent_blocks_example.exe" | Out-String
$independentMetrics = Extract-Metrics $independentOutput

Write-Host ""
Write-Host "COMPREHENSIVE COMPARISON RESULTS" -ForegroundColor Green
Write-Host "================================" -ForegroundColor Green
Write-Host ""

# Display performance comparison table
Write-Host "PERFORMANCE METRICS:" -ForegroundColor White
Write-Host ("{'0,-25} {'1,12} {'2,15} {'3,18}" -f "Metric", "Frame API", "Raw Streaming", "Independent Blocks")
Write-Host ("-" * 75)
Write-Host ("{'0,-25} {'1,9} ms {'2,12} ms {'3,15} ms" -f "Compression Time:", $frameMetrics.CompressionTime, $rawMetrics.CompressionTime, $independentMetrics.CompressionTime)
Write-Host ("{'0,-25} {'1,9} ms {'2,12} ms {'3,15} ms" -f "Decompression Time:", $frameMetrics.DecompressionTime, $rawMetrics.DecompressionTime, $independentMetrics.DecompressionTime)
Write-Host ("{'0,-25} {'1,12} {'2,15} {'3,12} ms" -f "Random Access Time:", "N/A", "N/A", $independentMetrics.RandomAccessTime)
Write-Host ""

Write-Host "COMPRESSION EFFICIENCY:" -ForegroundColor White
Write-Host ("{'0,-25} {'1,9}:1 {'2,12}:1 {'3,15}:1" -f "Compression Ratio:", $frameMetrics.Ratio, $rawMetrics.Ratio, $independentMetrics.Ratio)
Write-Host ("{'0,-25} {'1,10}% {'2,13}% {'3,16}%" -f "Space Saved:", $frameMetrics.SpaceSaved, $rawMetrics.SpaceSaved, $independentMetrics.SpaceSaved)
Write-Host ("{'0,-25} {'1,8} KB {'2,11} KB {'3,14} KB" -f "Compressed Size:", [math]::Round($frameMetrics.CompressedSize/1024), [math]::Round($rawMetrics.CompressedSize/1024), [math]::Round($independentMetrics.CompressedSize/1024))
Write-Host ""

Write-Host "SPECIAL FEATURES:" -ForegroundColor White
Write-Host ("{'0,-25} {'1,12} {'2,15} {'3,18}" -f "Block Count:", "Linked", "Linked", $independentMetrics.BlockCount)
Write-Host ("{'0,-25} {'1,12} {'2,15} {'3,18}" -f "Random Access:", "No", "No", "Yes")
Write-Host ("{'0,-25} {'1,12} {'2,15} {'3,18}" -f "Fault Tolerance:", "Low", "Low", "High")
Write-Host ("{'0,-25} {'1,12} {'2,15} {'3,18}" -f "Standard Format:", "Yes", "No", "No")
Write-Host ""

Write-Host "PERFORMANCE WINNERS:" -ForegroundColor Cyan
Write-Host "===================" -ForegroundColor Cyan

# Find winners
$fastestCompression = @($frameMetrics.CompressionTime, $rawMetrics.CompressionTime, $independentMetrics.CompressionTime) | Measure-Object -Minimum
$fastestDecompression = @($frameMetrics.DecompressionTime, $rawMetrics.DecompressionTime, $independentMetrics.DecompressionTime) | Measure-Object -Minimum
$bestRatio = @($frameMetrics.Ratio, $rawMetrics.Ratio, $independentMetrics.Ratio) | Measure-Object -Maximum

if ($frameMetrics.CompressionTime -eq $fastestCompression.Minimum) {
    Write-Host "🏆 Fastest Compression: Frame API ($($frameMetrics.CompressionTime) ms)" -ForegroundColor Green
} elseif ($rawMetrics.CompressionTime -eq $fastestCompression.Minimum) {
    Write-Host "🏆 Fastest Compression: Raw Streaming ($($rawMetrics.CompressionTime) ms)" -ForegroundColor Green
} else {
    Write-Host "🏆 Fastest Compression: Independent Blocks ($($independentMetrics.CompressionTime) ms)" -ForegroundColor Green
}

if ($frameMetrics.DecompressionTime -eq $fastestDecompression.Minimum) {
    Write-Host "🏆 Fastest Decompression: Frame API ($($frameMetrics.DecompressionTime) ms)" -ForegroundColor Green
} elseif ($rawMetrics.DecompressionTime -eq $fastestDecompression.Minimum) {
    Write-Host "🏆 Fastest Decompression: Raw Streaming ($($rawMetrics.DecompressionTime) ms)" -ForegroundColor Green
} else {
    Write-Host "🏆 Fastest Decompression: Independent Blocks ($($independentMetrics.DecompressionTime) ms)" -ForegroundColor Green
}

if ($frameMetrics.Ratio -eq $bestRatio.Maximum) {
    Write-Host "🏆 Best Compression Ratio: Frame API ($($frameMetrics.Ratio):1)" -ForegroundColor Green
} elseif ($rawMetrics.Ratio -eq $bestRatio.Maximum) {
    Write-Host "🏆 Best Compression Ratio: Raw Streaming ($($rawMetrics.Ratio):1)" -ForegroundColor Green
} else {
    Write-Host "🏆 Best Compression Ratio: Independent Blocks ($($independentMetrics.Ratio):1)" -ForegroundColor Green
}

Write-Host "🏆 Unique Feature: Independent Blocks - Random Access ($($independentMetrics.RandomAccessTime) ms for 7 rows)" -ForegroundColor Green

Write-Host ""
Write-Host "USE CASE RECOMMENDATIONS:" -ForegroundColor Magenta
Write-Host "=========================" -ForegroundColor Magenta
Write-Host "📁 Frame API: Standard compression, maximum compatibility" -ForegroundColor White
Write-Host "⚡ Raw Streaming: Maximum performance, custom protocols" -ForegroundColor White
Write-Host "🎯 Independent Blocks: Random access, fault tolerance, searchable data" -ForegroundColor White

Write-Host ""
Write-Host "TECHNICAL SUMMARY:" -ForegroundColor Yellow
Write-Host "==================" -ForegroundColor Yellow
Write-Host "• Frame API: Uses lz4frame.h with linked blocks for best compression"
Write-Host "• Raw Streaming: Uses lz4.h with dictionary continuity for speed"
Write-Host "• Independent Blocks: Uses lz4.h with isolated blocks for resilience"
Write-Host "• All approaches maintain data integrity and provide excellent compression"
Write-Host "• Independent Blocks enable new capabilities: random access and fault recovery"
