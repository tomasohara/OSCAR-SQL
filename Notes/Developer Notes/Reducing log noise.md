Yes, this is a great approach to managing log noise! Here's a clean model for implementing "first N occurrences" logging in Qt:

**Option 1: Simple Static Counter (Most Common)**

```cpp
// In your header or common logging utility
#include <QDebug>
#include <QHash>
#include <QString>

class LogLimiter {
public:
    static bool shouldLog(const QString& key, int maxCount = 10) {
        static QHash<QString, int> counters;
        
        int& count = counters[key];
        if (count < maxCount) {
            count++;
            return true;
        }
        return false;
    }
    
    static void reset(const QString& key = QString()) {
        static QHash<QString, int> counters;
        if (key.isEmpty()) {
            counters.clear();
        } else {
            counters.remove(key);
        }
    }
};

// Usage:
if (LogLimiter::shouldLog("database_connection")) {
    qDebug() << "Database connection attempt";
}
```

**Option 2: Macro-Based Approach (More Convenient)**

```cpp
// In a common header
#define qDebugLimited(key, maxCount) \
    if (LogLimiter::shouldLog(key, maxCount)) qDebug()

// Usage:
qDebugLimited("network_error", 10) << "Failed to connect to server";
```

**Option 3: Enhanced Version with Count Display**

```cpp
class LogLimiter {
public:
    static bool shouldLog(const QString& key, int maxCount = 10, int* outCount = nullptr) {
        static QHash<QString, int> counters;
        
        int& count = counters[key];
        count++;
        
        if (outCount) *outCount = count;
        
        if (count <= maxCount) {
            return true;
        } else if (count == maxCount + 1) {
            // Log one final message indicating suppression
            qDebug() << QString("Suppressing further '%1' messages").arg(key);
            return false;
        }
        return false;
    }
    
    static void reset(const QString& key = QString()) {
        static QHash<QString, int> counters;
        if (key.isEmpty()) {
            counters.clear();
        } else {
            counters.remove(key);
        }
    }
    
    static int getCount(const QString& key) {
        static QHash<QString, int> counters;
        return counters.value(key, 0);
    }
};

// Usage:
int count;
if (LogLimiter::shouldLog("parsing_warning", 10, &count)) {
    qDebug() << QString("[%1/10]").arg(count) << "Parse warning: invalid token";
}
```

**Key Design Decisions:**

1. **String keys**: Use descriptive keys for different log points
2. **Static storage**: Counters persist across calls
3. **Thread safety**: If you're using multiple threads, wrap access in `QMutex`
4. **Reset capability**: Useful for debugging sessions or test boundaries

**Thread-Safe Version:**

```cpp
class LogLimiter {
public:
    static bool shouldLog(const QString& key, int maxCount = 10) {
        static QHash<QString, int> counters;
        static QMutex mutex;
        
        QMutexLocker locker(&mutex);
        int& count = counters[key];
        if (count < maxCount) {
            count++;
            return true;
        }
        return false;
    }
};
```

**Pro tip**: You can also use `__FILE__` and `__LINE__` to auto-generate unique keys:

```cpp
#define qDebugLimited10() \
    if (LogLimiter::shouldLog(QString("%1:%2").arg(__FILE__).arg(__LINE__), 10)) qDebug()
```

This gives you a zero-configuration approach where each call site automatically gets its own limit.

Which approach fits best with your existing codebase?