# Multiple EventLists Per Channel Analysis

**Date:** January 1, 2026  
**Copyright (c) 2026 The OSCAR Team**

## Summary

The code DOES create multiple EventLists for a single channel. This occurs primarily in the ResMed loader when converting waveform data to OSCAR's time-delta event format.

## Data Structure Support

The session data structure explicitly supports multiple EventLists per channel:

```cpp
// From session.h
QHash<ChannelID, QVector<EventList *> > eventlist;
```

The key is a `ChannelID`, and the value is a `QVector` of `EventList` pointers, allowing multiple EventLists per channel.

## When Multiple EventLists Are Created

### Primary Case: Out-of-Bounds Values

In `oscar/SleepLib/loader_plugins/resmed_loader.cpp`, the `ToTimeDelta()` function and helper `buildEventList()` function create multiple EventLists when data values exceed physical bounds:

**Location:** `buildEventList()` function (around line 3850)

```cpp
EventList * buildEventList(EventStoreType est, EventDataType t_min, EventDataType t_max, 
                          EDFSignal &es, EventDataType *min, EventDataType *max, 
                          double tt, EventList *el, Session * sess, ChannelID code)
{
    EventDataType tmp = EventDataType(est) * es.gain;

    if ((tmp >= t_min) && (tmp <= t_max)) {
        // Value is in range - add to current EventList
        if (tmp < *min) *min = tmp;
        if (tmp > *max) *max = tmp;
        el->AddEvent(tt, est);
    } else {
        // OUT OF BOUNDS - Start a new EventList for the same channel
        qDebug() << "Value:" << tmp << "Out of range - creating new EventList";
        
        // Close current EventList with duplicate of last value
        el->AddEvent(tt, el->raw(el->count() - 1));
        
        if (el->count() > 1) {
            el->setDimension(es.physical_dimension);
            
            // CREATE NEW EVENTLIST FOR SAME CHANNEL
            el = sess->AddEventList(code, EVL_Event, es.gain, es.offset, 0, 0);
        } else {
            el->clear(); // Reuse if only one value
        }
    }
    return el;
}
```

### Why This Happens

1. **Data Integrity:** When ResMed devices produce values outside expected physical ranges (e.g., pressure spikes, glitches), the code segregates them into separate EventLists rather than corrupting statistics
2. **Robust Statistics:** Each EventList maintains its own min/max values, preventing outliers from affecting the main data stream
3. **Visual Separation:** Multiple EventLists allow the graphing code to potentially handle anomalous data differently

### Specific Scenarios

The code comments indicate this happens with:

- **Pressure channels** (CPAP_Pressure, CPAP_IPAP, CPAP_EPAP) when values exceed machine limits
- **Flow rate data** when sensor glitches occur  
- **Any waveform channel** where physical_minimum and physical_maximum bounds are exceeded

## Code Flow

1. **ResMedLoader::ToTimeDelta()** calls `buildEventList()` for each data sample
2. **buildEventList()** checks if value is within `[t_min, t_max]` range
3. **If out of range:**
   - Current EventList is closed (with a duplicate final value for continuity)
   - `sess->AddEventList()` is called with the **same channel code**
   - New EventList starts fresh with reset min/max tracking
4. **Process continues** with the new EventList

## AddEventList Implementation

From `session.cpp`:

```cpp
EventList *Session::AddEventList(ChannelID code, EventListType et, 
                                 EventDataType gain, EventDataType offset, 
                                 EventDataType min, EventDataType max, 
                                 EventDataType rate, bool second_field)
{
    EventList *el = new EventList(et, gain, offset, min, max, rate, second_field);
    
    // ALWAYS APPENDS - never checks for existing EventList
    eventlist[code].push_back(el);
    
    return el;
}
```

**Key Point:** The function `push_back()` always appends, never replacing. No check is made for existing EventLists.

## File Format Support

The .001 file format explicitly supports this design:

```
FOR EACH CHANNEL:
  ├─ channelId (quint32)
  ├─ eventListCount (qint16)        <-- Can be > 1
  └─ FOR EACH EVENTLIST:
      ├─ [metadata for this eventlist]
```

## Why You May Not See Multiple EventLists Often

1. **Good Data Quality:** Most CPAP sessions produce data within normal ranges
2. **Well-Calibrated Devices:** Modern devices rarely produce out-of-range values
3. **Physical Limits Are Wide:** The t_min/t_max bounds are set conservatively to accommodate normal variations

## Implications for Developers

### Reading Sessions
```cpp
QHash<ChannelID, QVector<EventList *> >::iterator it = eventlist.find(channelID);
if (it != eventlist.end()) {
    QVector<EventList *> &eventLists = it.value();
    
    // MUST iterate through ALL EventLists for this channel
    for (int i = 0; i < eventLists.size(); ++i) {
        EventList *el = eventLists[i];
        // Process this EventList
    }
}
```

### Statistics Calculation

Session statistics functions (min, max, count, etc.) already handle multiple EventLists correctly by iterating through the entire vector - see `session.cpp` functions like `Session::Min()`, `Session::Max()`, etc.

## Testing Recommendation

To verify multiple EventList creation:

1. **Create test data** with values that exceed physical bounds
2. **Import a session** with known data glitches
3. **Check the .001 file** - look for `eventListCount > 1` for any channel
4. **Add debug logging** in `buildEventList()` to track when new EventLists are created

## Conclusion

**Multiple EventLists per channel ARE created by the code**, specifically in the ResMed loader when processing waveform data that exceeds physical bounds. This is a design feature for data integrity, not a bug. The file format, data structures, and I/O code all support this capability.

You may not commonly see it because:
- Most data stays within bounds
- The feature activates only during anomalous readings
- It's working as designed to isolate bad data

## Related Files

- `oscar/SleepLib/session.h` - Data structure definition
- `oscar/SleepLib/session.cpp` - AddEventList() implementation  
- `oscar/SleepLib/loader_plugins/resmed_loader.cpp` - ToTimeDelta() and buildEventList()
- `Notes/.001 Events File Format Documentation-revised.md` - File format specification
