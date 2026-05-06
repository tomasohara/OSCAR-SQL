# Qt ComboBox Signal Change for exportCSV.cpp

**Date:** January 22, 2026  
**Module:** exportCSV (Export CSV Dialog)  
**Issue:** Need to respond to combo box value changes, not just user activation

## Background

The `quickRangeCombo` combo box currently uses the `activated` signal, which only fires when a user explicitly selects an item from the dropdown. This doesn't trigger when:
- The value is set programmatically via `setCurrentIndex()` or `setCurrentText()`
- The selection changes due to other code

## Qt ComboBox Signals (Qt6)

QComboBox provides several signals for different use cases:

1. **`activated(int index)` / `activated(const QString &text)`**
   - Emitted ONLY when user selects an item from the dropdown
   - Does NOT fire on programmatic changes
   - Current implementation uses this

2. **`currentIndexChanged(int index)` / `currentTextChanged(const QString &text)`**
   - Emitted whenever the current item changes
   - Fires on BOTH user interaction AND programmatic changes
   - **RECOMMENDED** for most use cases where you need to respond to any change

3. **`textActivated(const QString &text)` / `textHighlighted(const QString &text)`**
   - Qt6 specific variants
   - Similar behavior to `activated` and `highlighted`

## Solution

Change from `activated` signal to `currentTextChanged` signal.

### Qt Auto-Connection Convention

Qt Designer uses automatic signal-slot connection based on naming convention:
- Pattern: `on_<objectName>_<signalName>`
- Qt automatically connects these at runtime when using `.ui` files
- No explicit `connect()` call needed

### Changes Required

1. **In exportcsv.h:**
   - Rename: `on_quickRangeCombo_activated` → `on_quickRangeCombo_currentTextChanged`

2. **In exportcsv.cpp:**
   - Rename method implementation to match
   - No changes to method logic needed
   - Remove any manual call to this method in constructor (should rely on signal)

### Alternative: currentIndexChanged

If you prefer working with indices instead of text:
```cpp
void ExportCSV::on_quickRangeCombo_currentIndexChanged(int index)
{
    QString arg1 = ui->quickRangeCombo->currentText();
    // ... rest of logic
}
```

## Implementation Notes

- The current implementation manually calls `on_quickRangeCombo_activated(tr("Most Recent Day"))` in the constructor
- With the new signal, this will fire automatically when the default index is set
- However, if you need to ensure initialization before the widget is fully connected, the manual call is acceptable
- The method name for the manual call should match the new slot name

## Testing

After changes, verify:
1. User selection from dropdown triggers the handler
2. Programmatic changes via `setCurrentIndex()` trigger the handler
3. Initial setup in constructor works correctly
4. Date range fields enable/disable appropriately for "Custom" selection
5. Date ranges populate correctly for all preset options

## References

- Qt6 QComboBox Documentation: https://doc.qt.io/qt-6/qcombobox.html#signals
- Qt Designer Auto-Connection: https://doc.qt.io/qt-6/designer-using-a-ui-file.html#automatic-connections
