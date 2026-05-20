# OSCAR Database Indexes and Foreign Key Relationships

---

## Database Indexes

### Profile & Machine Indexes
```sql
idx_profiles_username ON profiles(username)
idx_machines_profile ON machines(profile_id)
idx_machines_serial ON machines(serial_number)
idx_machines_loader ON machines(loader_name)
```

### User & Doctor Indexes
```sql
idx_user_info_profile ON user_info(profile_id)
idx_doctor_info_profile ON doctor_info(profile_id)
```

### Preferences Indexes
```sql
idx_preferences_profile ON profile_preferences(profile_id)
idx_preferences_category ON profile_preferences(profile_id, category)
idx_preferences_key ON profile_preferences(profile_id, category, key)
```

### Session Indexes
```sql
idx_sessions_machine ON sessions(machine_id)
idx_sessions_time ON sessions(start_time, end_time)
idx_sessions_enabled ON sessions(machine_id, enabled)
```

### Session Data Indexes
```sql
idx_session_settings_session ON session_settings(session_id)
idx_session_settings_channel ON session_settings(session_id, channel_id)
idx_session_channels_session ON session_channels(session_id)
idx_session_channels_channel ON session_channels(channel_id)
idx_session_channels_lookup ON session_channels(session_id, channel_id)
```

### Event Indexes
```sql
idx_respiratory_events_session ON respiratory_events(session_id)
idx_respiratory_events_type ON respiratory_events(session_id, event_type)
idx_respiratory_events_time ON respiratory_events(start_time, end_time)
```

### Summary & Slice Indexes
```sql
idx_session_summaries_session ON session_summaries(session_id)
idx_session_summaries_ahi ON session_summaries(ahi)
idx_session_slices_session ON session_slices(session_id)
idx_session_slices_time ON session_slices(start_time, end_time)
```

### Channel Indexes
```sql
idx_channels_profile ON channels(profile_id)
idx_channels_code ON channels(channel_code)
idx_channels_lookup ON channels(profile_id, channel_id)
idx_channel_options_channel ON channel_options(channel_id)
idx_channel_options_lookup ON channel_options(channel_id, option_key)
```

### Daily Summaries Indexes ⭐ NEW IN v6 (profile_machine index dropped in v16)
```sql
idx_daily_summaries_profile_date ON daily_summaries(profile_id, date)
idx_daily_summaries_ahi ON daily_summaries(ahi)
idx_daily_summaries_compliance ON daily_summaries(profile_id, is_compliant)
idx_daily_summaries_date_range ON daily_summaries(profile_id, date DESC)
```

### Event Data Indexes ⚡ NEW IN v8
```sql
idx_event_lists_session ON event_lists(session_id)
idx_event_lists_channel ON event_lists(session_id, channel_id)
idx_event_lists_type ON event_lists(event_type)
idx_event_lists_time ON event_lists(session_id, first_time, last_time)
idx_event_data_eventlist ON event_data(eventlist_id)
```

### Profile ID Denormalization Indexes 🔧 NEW IN v12
```sql
idx_session_summaries_profile ON session_summaries(profile_id)
idx_session_summaries_profile_date ON session_summaries(profile_id, session_id)
idx_session_settings_profile ON session_settings(profile_id)
idx_session_settings_profile_channel ON session_settings(profile_id, channel_id)
idx_session_channels_profile ON session_channels(profile_id)
idx_session_channels_profile_channel ON session_channels(profile_id, channel_id)
idx_event_lists_profile ON event_lists(profile_id)
idx_event_lists_profile_channel ON event_lists(profile_id, channel_id)
idx_respiratory_events_profile ON respiratory_events(profile_id)
idx_respiratory_events_profile_type ON respiratory_events(profile_id, event_type)
```

### Report Tree Indexes 🌲 NEW IN v13
```sql
idx_report_tree_parent ON report_tree(parent_id)
idx_report_tree_source ON report_tree(source)
idx_report_tree_type ON report_tree(node_type)
```

---

## Entity Relationship Diagram

```
profiles (1) ──┬─< machines (N)
               ├─< user_info (1)
               ├─< doctor_info (1)
               ├─< profile_preferences (N)
               ├─< channels (N)
               ├─< daily_summaries (N)
               ├─< session_settings (N)    [denormalized profile_id, v12]
               ├─< session_channels (N)    [denormalized profile_id, v12]
               ├─< session_summaries (N)   [denormalized profile_id, v12]
               ├─< event_lists (N)         [denormalized profile_id, v12]
               └─< respiratory_events (N)  [denormalized profile_id, v12]

machines (1) ──── sessions (N)

sessions (1) ──┬─< session_settings (N)
               ├─< session_channels (N)
               ├─< respiratory_events (N)
               ├─< session_summaries (1)
               ├─< session_slices (N)
               └─< event_lists (N)

event_lists (1) ─< event_data (1)

session_channels (1) ─< session_channel_values (N)

report_tree (1) ─< report_tree (N)   [self-referencing parent_id, v13]

channel_options (N) - standalone (references channel_id constant)
report_tree (N) - global (not profile-specific) 🌲 NEW IN v13
```

---

## Foreign Key Details

| Child Table | FK Column | Parent Table | Parent Column | On Delete |
|-------------|-----------|--------------|---------------|-----------|
| machines | profile_id | profiles | id | CASCADE |
| user_info | profile_id | profiles | id | CASCADE |
| doctor_info | profile_id | profiles | id | CASCADE |
| profile_preferences | profile_id | profiles | id | CASCADE |
| channels | profile_id | profiles | id | CASCADE |
| daily_summaries | profile_id | profiles | id | CASCADE |
| sessions | machine_id | machines | id | CASCADE |
| session_settings | session_id | sessions | id | CASCADE |
| session_settings | profile_id | profiles | id | CASCADE |
| session_channels | session_id | sessions | id | CASCADE |
| session_channels | profile_id | profiles | id | CASCADE |
| session_channel_values | session_channel_id | session_channels | id | CASCADE |
| respiratory_events | session_id | sessions | id | CASCADE |
| respiratory_events | profile_id | profiles | id | CASCADE |
| session_summaries | session_id | sessions | id | CASCADE |
| session_summaries | profile_id | profiles | id | CASCADE |
| session_slices | session_id | sessions | id | CASCADE |
| event_lists | session_id | sessions | id | CASCADE |
| event_lists | profile_id | profiles | id | CASCADE |
| event_data | eventlist_id | event_lists | id | CASCADE |
| report_tree | parent_id | report_tree | id | CASCADE |

---

## Cascade Delete Behavior

- **Deleting profile** removes: machines, user_info, doctor_info, preferences, channels, daily_summaries, session_settings, session_channels, session_summaries, event_lists, respiratory_events (all denormalized profile_id FKs)

- **Deleting machine** removes: sessions (and their data). `daily_summaries` rows survive, since v16 they no longer reference a machine.

- **Deleting session** removes: settings, channels, events, summaries, slices, event_lists (which cascades to event_data)

- **Deleting event_list** removes: event_data (waveform/event binary data)

- **Deleting report_tree node** removes: all child nodes (self-referencing cascade)
