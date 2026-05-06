# Percentile Calculation Interpolation Design

**Date:** January 29, 2026  
**Author:** Senior Developer  
**Copyright:** (c) 2026 The OSCAR Team

## Problem Statement

OSCAR was experiencing statistical discrepancies when comparing session-level percentile results with day-level percentile results. The root cause was that OSCAR had multiple percentile calculation methods with inconsistent interpolation behavior:

1. **Day::percentile()** (in day.cpp) - **Uses linear interpolation** between values
2. **Session::percentile()** (in session.cpp) - **No interpolation** - returns exact value at index
3. **Session::calculatePercentiles()** (in session.cpp) - **No interpolation** - returns exact value at index

This inconsistency could cause confusion when analyzing data, as the same underlying data would produce different statistical results depending on whether it was calculated at the session level or aggregated at the day level.

## Solution

Modified `Session::calculatePercentiles()` to return **interpolated percentile values** for consistency with `Day::percentile()`.

### Implementation Details

#### Before (Non-Interpolated)
```cpp
// Calculate indices for each percentile
int idx50 = static_cast<int>(dataSize * 0.50);

// Use nth_element to find value at exact index
std::nth_element(combinedData.begin(), combinedData.begin() + idx50, combinedData.end());
result.median = combinedData[idx50] * gain;
```

#### After (Interpolated)
```cpp
// Fully sort the array once for interpolated percentile calculations
std::sort(combinedData.begin(), combinedData.end());

auto calcInterpolatedPercentile = [&](double percentile) -> EventDataType {
    // Calculate the exact position in the sorted array
    double position = (dataSize - 1) * percentile;
    int lowerIndex = static_cast<int>(floor(position));
    int upperIndex = static_cast<int>(ceil(position));
    
    // If indices are the same, no interpolation needed
    if (lowerIndex == upperIndex) {
        return combinedData[lowerIndex] * gain;
    }
    
    // Linear interpolation between the two values
    EventDataType lowerValue = combinedData[lowerIndex] * gain;
    EventDataType upperValue = combinedData[upperIndex] * gain;
    double fraction = position - lowerIndex;
    
    return lowerValue + (upperValue - lowerValue) * fraction;
};
```

### Interpolation Formula

When a percentile value falls between two data points in the sorted array:

```
result = v1 + (v2 - v1) * fraction
```

Where:
- `v1` = value at the lower index (floor)
- `v2` = value at the upper index (ceil)  
- `fraction` = decimal portion of the position (how far between v1 and v2)

### Example

For a dataset with 100 samples, the 95th percentile:
- Position = (100 - 1) * 0.95 = 94.05
- Lower index = 94, Upper index = 95
- Fraction = 0.05
- If values are [10.2, 10.8]:
  - Result = 10.2 + (10.8 - 10.2) * 0.05 = 10.23

## Performance Considerations

### Sorting Cost
The change from `nth_element` to `std::sort` increases computational complexity:
- **Before:** O(n) for each percentile using nth_element
- **After:** O(n log n) for one-time sort, then O(1) for each percentile lookup

### Overall Impact
For calculating multiple percentiles (median, p90, p95, p995):
- **Before:** 4 × O(n) = O(4n) with partial sorting optimization
- **After:** O(n log n) + 4 × O(1) = O(n log n)

For typical dataset sizes (1,000-10,000 samples), the difference is negligible (milliseconds), and the benefit of accurate interpolation outweighs the small performance cost.

## Affected Functions

### Modified
- `Session::calculatePercentiles()` - Now uses interpolation (for multiple percentiles efficiently)
- `Session::percentile()` - Now uses interpolation (for single percentile calculations)

### Reference Implementation
- `Day::percentile()` - Already uses interpolation (reference implementation that others now match)

## Benefits

1. **Consistency** - Session and day statistics now match
2. **Accuracy** - Interpolated percentiles are statistically more accurate than nearest-value percentiles
3. **Standards Compliance** - Linear interpolation is the standard method for percentile calculation in statistical software

## Testing Recommendations

1. Compare session-level statistics with day-level statistics for the same data
2. Verify that median, p90, p95, and p995 values are reasonable
3. Check edge cases:
   - Single value datasets
   - Small datasets (< 10 values)
   - Large datasets (> 100,000 values)
   - Datasets with many duplicate values

## Future Considerations

The `Session::percentile()` function (single percentile) could also be updated to use interpolation for complete consistency across the codebase. However, this change was scoped to `calculatePercentiles()` as requested, which is the primary function used for database storage of percentile statistics.

## References

- Original issue: Discrepancies between session and day statistical results
- Related code: 
  - `oscar/SleepLib/day.cpp` - Day::percentile() (reference implementation)
  - `oscar/SleepLib/session.cpp` - Session::calculatePercentiles() (modified)
  - `oscar/SleepLib/session.h` - PercentilesResult structure

## Version History

- **v1.0** (2026-01-29) - Initial implementation with linear interpolation
