#!/usr/bin/env pwsh

Write-Host "LZ4 Performance Comparison: Frame API vs Raw Streaming API" -ForegroundColor Cyan
Write-Host "============================================================" -ForegroundColor Cyan
Write-Host ""

# Run Frame API example multiple times for average
Write-Host "Testing LZ4 Frame API..." -ForegroundColor Yellow
$frameResults = @()
for ($i = 1; $i -le 3; $i++) {
    Write-Host "  Run $i/3..." -ForegroundColor Gray
    $output = & ".\build\bin\Release\lz4_streaming_file_example.exe" | Out-String
    
    # Extract performance metrics
    if ($output -match "Compression time: (\d+) ms") { 
        $compressionTime = [int]$matches[1] 
    }
    if ($output -match "Decompression time: (\d+) ms") { 
        $decompressionTime = [int]$matches[1] 
    }
    if ($output -match "Compression ratio: ([\d.]+):1") { 
        $compressionRatio = [double]$matches[1] 
    }
    if ($output -match "Space saved: ([\d.]+)%") { 
        $spaceSaved = [double]$matches[1] 
    }
    if ($output -match "Compressed file size: (\d+) bytes") { 
        $compressedSize = [int]$matches[1] 
    }
    
    $frameResults += @{
        Compression = $compressionTime
        Decompression = $decompressionTime
        Ratio = $compressionRatio
        SpaceSaved = $spaceSaved
        CompressedSize = $compressedSize
    }
}

Write-Host ""
Write-Host "Testing LZ4 Raw Streaming API..." -ForegroundColor Yellow
$rawResults = @()
for ($i = 1; $i -le 3; $i++) {
    Write-Host "  Run $i/3..." -ForegroundColor Gray
    $output = & ".\build\bin\Release\lz4_raw_streaming_example.exe" | Out-String
    
    # Extract performance metrics
    if ($output -match "Compression time: (\d+) ms") { 
        $compressionTime = [int]$matches[1] 
    }
    if ($output -match "Decompression time: (\d+) ms") { 
        $decompressionTime = [int]$matches[1] 
    }
    if ($output -match "Compression ratio: ([\d.]+):1") { 
        $compressionRatio = [double]$matches[1] 
    }
    if ($output -match "Space saved: ([\d.]+)%") { 
        $spaceSaved = [double]$matches[1] 
    }
    if ($output -match "Compressed file size: (\d+) bytes") { 
        $compressedSize = [int]$matches[1] 
    }
    
    $rawResults += @{
        Compression = $compressionTime
        Decompression = $decompressionTime
        Ratio = $compressionRatio
        SpaceSaved = $spaceSaved
        CompressedSize = $compressedSize
    }
}

# Calculate averages
$frameAvg = @{
    Compression = ($frameResults | Measure-Object -Property Compression -Average).Average
    Decompression = ($frameResults | Measure-Object -Property Decompression -Average).Average
    Ratio = ($frameResults | Measure-Object -Property Ratio -Average).Average
    SpaceSaved = ($frameResults | Measure-Object -Property SpaceSaved -Average).Average
    CompressedSize = ($frameResults | Measure-Object -Property CompressedSize -Average).Average
}

$rawAvg = @{
    Compression = ($rawResults | Measure-Object -Property Compression -Average).Average
    Decompression = ($rawResults | Measure-Object -Property Decompression -Average).Average
    Ratio = ($rawResults | Measure-Object -Property Ratio -Average).Average
    SpaceSaved = ($rawResults | Measure-Object -Property SpaceSaved -Average).Average
    CompressedSize = ($rawResults | Measure-Object -Property CompressedSize -Average).Average
}

# Display results
Write-Host ""
Write-Host "PERFORMANCE COMPARISON RESULTS" -ForegroundColor Green
Write-Host "==============================" -ForegroundColor Green
Write-Host ""

Write-Host "COMPRESSION PERFORMANCE:" -ForegroundColor White
Write-Host ("  Frame API:       {0:F1} ms" -f $frameAvg.Compression)
Write-Host ("  Raw Streaming:   {0:F1} ms" -f $rawAvg.Compression)
$compressionDiff = (($rawAvg.Compression - $frameAvg.Compression) / $frameAvg.Compression) * 100
if ($compressionDiff -gt 0) {
    Write-Host ("  Difference:      +{0:F1}% (Raw Streaming slower)" -f $compressionDiff) -ForegroundColor Red
} else {
    Write-Host ("  Difference:      {0:F1}% (Raw Streaming faster)" -f $compressionDiff) -ForegroundColor Green
}

Write-Host ""
Write-Host "DECOMPRESSION PERFORMANCE:" -ForegroundColor White
Write-Host ("  Frame API:       {0:F1} ms" -f $frameAvg.Decompression)
Write-Host ("  Raw Streaming:   {0:F1} ms" -f $rawAvg.Decompression)
$decompressionDiff = (($rawAvg.Decompression - $frameAvg.Decompression) / $frameAvg.Decompression) * 100
if ($decompressionDiff -gt 0) {
    Write-Host ("  Difference:      +{0:F1}% (Raw Streaming slower)" -f $decompressionDiff) -ForegroundColor Red
} else {
    Write-Host ("  Difference:      {0:F1}% (Raw Streaming faster)" -f $decompressionDiff) -ForegroundColor Green
}

Write-Host ""
Write-Host "COMPRESSION RATIO:" -ForegroundColor White
Write-Host ("  Frame API:       {0:F2}:1 ({1:F1}% space saved)" -f $frameAvg.Ratio, $frameAvg.SpaceSaved)
Write-Host ("  Raw Streaming:   {0:F2}:1 ({1:F1}% space saved)" -f $rawAvg.Ratio, $rawAvg.SpaceSaved)
$ratioDiff = (($rawAvg.Ratio - $frameAvg.Ratio) / $frameAvg.Ratio) * 100
if ($ratioDiff -gt 0) {
    Write-Host ("  Difference:      +{0:F1}% (Raw Streaming better compression)" -f $ratioDiff) -ForegroundColor Green
} else {
    Write-Host ("  Difference:      {0:F1}% (Frame API better compression)" -f $ratioDiff) -ForegroundColor Red
}

Write-Host ""
Write-Host "FILE SIZE COMPARISON:" -ForegroundColor White
Write-Host ("  Frame API:       {0:F1} KB" -f ($frameAvg.CompressedSize / 1024))
Write-Host ("  Raw Streaming:   {0:F1} KB" -f ($rawAvg.CompressedSize / 1024))
$sizeDiff = $rawAvg.CompressedSize - $frameAvg.CompressedSize
Write-Host ("  Difference:      {0:F1} KB" -f ($sizeDiff / 1024))

Write-Host ""
Write-Host "SUMMARY:" -ForegroundColor Cyan
Write-Host "  • Frame API provides better compression ratios due to optimized frame format"
Write-Host "  • Raw Streaming API gives more control but requires custom format handling"
Write-Host "  • Frame API includes built-in checksums and metadata"
Write-Host "  • Raw Streaming API requires manual block management and checksums"
Write-Host "  • Both maintain compression dictionary across blocks for related data"
