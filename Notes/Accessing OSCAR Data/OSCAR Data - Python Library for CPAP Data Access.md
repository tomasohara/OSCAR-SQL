# OSCAR Data - Python Library for CPAP Data Access

A Python library for accessing and analyzing CPAP therapy data from OSCAR's SQLite databases.

## Features

✨ **Simple, Pythonic API** - Easy to use for beginners, powerful for experts  
📊 **Lazy Loading** - Efficient handling of large waveform datasets  
🔄 **Smart Downsampling** - Automatic data reduction for manageable analysis  
⏱️ **Time-Based Slicing** - Analyze specific time windows  
📁 **Flexible Export** - CSV, JSON, NumPy formats  
🔍 **Memory Efficient** - Process data in chunks to avoid memory issues  
🎯 **Type Hints** - Full typing support for IDE autocomplete

## Installation

```bash
pip install oscar-data
```

For visualization support:
```bash
pip install oscar-data[viz]
```

For all optional features:
```bash
pip install oscar-data[all]
```

## Quick Start

```python
import oscar_data as oscar

# Open database
db = oscar.open_database("path/to/oscar.db")

# Get a session
session = db.get_session("2024-02-14")

# Access summary statistics
print(f"AHI: {session.ahi}")
print(f"Leak rate: {session.leak_rate} L/min")

# Get high-frequency data (automatically downsampled)
flow = session.flow_rate.resample('1s')  # 1-second averages
pressure = session.pressure.resample('1s')

# Access events
apneas = session.events.apneas
print(f"Total apneas: {len(apneas)}")
```

## Core Concepts

### Database
The entry point to your OSCAR data:

```python
db = oscar.open_database("path/to/oscar.db")
sessions = db.get_sessions()
session = db.get_session("2024-02-14")
```

### Session
Represents one night of CPAP therapy:

```python
session = db.get_session("2024-02-14")

# Summary statistics
session.ahi              # Apnea-Hypopnea Index
session.leak_rate        # Median leak rate
session.pressure_95th    # 95th percentile pressure

# Duration and timing
session.start_time
session.end_time
session.duration
```

### Waveforms
High-frequency time-series data with lazy loading:

```python
# Access waveforms (doesn't load data yet)
flow = session.flow_rate
pressure = session.pressure

# Get downsampled data (this loads and processes)
flow_1s = flow.resample('1s')  # 1-second averages
flow_5s = flow.resample('5s')  # 5-second averages

# Raw data access (warning: may be very large!)
raw_data = flow.to_numpy()

# Memory-efficient iteration
for chunk in flow.iter_chunks(chunk_size=60):  # 60-second chunks
    mean = chunk.mean()
    print(f"Average flow: {mean}")
```

### Events
Detected respiratory events:

```python
events = session.events

# Access specific event types
events.apneas          # All apneas
events.hypopneas       # All hypopneas
events.clear_airway    # Clear airway apneas
events.obstructive     # Obstructive apneas

# Event details
for event in events.apneas[:5]:
    print(f"{event.type} at {event.start_time}, duration: {event.duration}s")
```

### Time-Based Slicing
Analyze specific time windows:

```python
# First hour of sleep
first_hour = session.slice(start='23:30', end='00:30')
flow = first_hour.flow_rate.resample('1s')

# Using seconds from session start
first_30min = session.slice(start=0, end=1800)

# Analyze the slice
print(f"AHI in first hour: {first_hour.statistics.ahi}")
```

## Advanced Usage

### Multi-Session Analysis

```python
from datetime import date

# Get sessions in a date range
sessions = db.get_sessions(
    start_date=date(2024, 2, 1),
    end_date=date(2024, 2, 29)
)

# Calculate trends
ahi_values = [s.ahi for s in sessions]
avg_ahi = sum(ahi_values) / len(ahi_values)

print(f"Average AHI: {avg_ahi:.2f}")
```

### Custom Analysis with NumPy

```python
import numpy as np

# Get raw data for custom processing
flow_data = session.flow_rate.to_numpy()

# Calculate custom metrics
breathing_rate = calculate_breathing_rate(flow_data)
flow_variability = np.std(flow_data)
```

### Exporting Data

```python
# Export summary statistics
session.export_summary('summary.csv')

# Export events
session.export_events('events.csv')

# Export waveform (with downsampling)
session.flow_rate.export('flow.csv', resolution='1s')

# Export everything for a time window
night_section = session.slice('01:00', '03:00')
night_section.export_all('output_dir/', downsample='1s')
```

### Signal Processing

```python
from oscar_data.filters import butter_lowpass

# Apply low-pass filter
filtered = session.flow_rate.apply(butter_lowpass, cutoff=0.5)
```

### Memory Management

```python
# Configure global memory limits
oscar.config.memory_limit = 1024 * 1024 * 1024  # 1 GB

# Process large datasets in chunks
for i, chunk in enumerate(session.flow_rate.iter_chunks(300)):  # 5-minute chunks
    # Process each chunk
    process_chunk(chunk)
```

## Understanding Blob Data Formats

The library handles binary blob decoding automatically, but you can access format information:

```python
from oscar_data.decoders import get_format_info

info = get_format_info('flow_rate')
print(info['description'])
print(info['encoding'])
print(info['scale_factor'])
print(info['typical_range'])
```

### Format Specifications

| Waveform Type | Encoding | Scale Factor | Unit | Sample Rate |
|--------------|----------|--------------|------|-------------|
| flow_rate | int16 LE | 100 | L/min | 25 Hz |
| pressure | uint16 LE | 10 | cmH2O | 25 Hz |
| leak_rate | int16 LE | 100 | L/min | 1 Hz |
| spo2 | uint8 | 1 | % | 1 Hz |

## Configuration

```python
# Set global defaults
oscar.config.default_downsample = '500ms'
oscar.config.cache_enabled = True
oscar.config.verbose = True
```

## API Reference

### Database Class

- `open_database(path)` - Open an OSCAR database
- `get_sessions(start_date, end_date)` - Get list of sessions
- `get_session(date)` - Get specific session
- `get_date_range()` - Get available date range
- `close()` - Close database connection

### Session Class

- Properties: `ahi`, `leak_rate`, `pressure_95th`, `start_time`, `end_time`, `duration`
- Waveforms: `flow_rate`, `pressure`, `leak_rate_waveform`, `spo2`
- Methods: `slice()`, `export_summary()`, `export_events()`, `export_all()`

### Waveform Class

- Properties: `sample_rate`, `shape`, `duration`
- Methods: `to_numpy()`, `resample()`, `slice()`, `iter_chunks()`, `export()`
- Indexing: `waveform[start:end]` for array-style access

### Events Class

- Properties: `apneas`, `hypopneas`, `clear_airway`, `obstructive`
- Methods: `export_csv()`, `slice()`

## Testing

```bash
# Run tests
pytest

# With coverage
pytest --cov=oscar_data
```

## Contributing

Contributions are welcome! Please see CONTRIBUTING.md for guidelines.

## License

GNU General Public License v3.0 - see LICENSE file

## Acknowledgments

Built for the OSCAR (Open Source CPAP Analysis Reporter) community.

## Support

- Documentation: https://oscar-data.readthedocs.io
- Issues: https://github.com/your-org/oscar-data/issues
- OSCAR Project: https://www.sleepfiles.com/OSCAR/

## Examples

See the `examples/` directory for more detailed usage examples:

- `basic_usage.py` - Common use cases
- `data_export.py` - Various export scenarios
- `visualization.py` - Plotting with matplotlib
- `custom_analysis.py` - Advanced analysis techniques