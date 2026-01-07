/* Performance Timer Utility
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * Lightweight performance instrumentation for identifying bottlenecks
 * during SD card import operations.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef PERFORMANCE_TIMER_H
#define PERFORMANCE_TIMER_H

#include <QElapsedTimer>
#include <QString>
#include <QDebug>
#include <QMap>
#include <QMutex>

// Enable/disable performance instrumentation
// Define ENABLE_PERFORMANCE_INSTRUMENTATION in the .pro file or here
#define ENABLE_PERFORMANCE_INSTRUMENTATION

#ifdef ENABLE_PERFORMANCE_INSTRUMENTATION
    #define PERF_TIMER_START(name) PerformanceTimer::instance().start(name)
    #define PERF_TIMER_STOP(name) PerformanceTimer::instance().stop(name)
    #define PERF_TIMER_SCOPE(name) PerformanceScopeTimer _perfTimer##__LINE__(name)
    #define PERF_TIMER_REPORT() PerformanceTimer::instance().report()
    #define PERF_TIMER_RESET() PerformanceTimer::instance().reset()
#else
    #define PERF_TIMER_START(name)
    #define PERF_TIMER_STOP(name)
    #define PERF_TIMER_SCOPE(name)
    #define PERF_TIMER_REPORT()
    #define PERF_TIMER_RESET()
#endif

/*!
 * \class PerformanceTimer
 * \brief Singleton class for tracking performance metrics during import
 *
 * Thread-safe performance timer that accumulates timing data across
 * multiple operations. Useful for identifying bottlenecks during
 * SD card import when switching to database storage.
 */
class PerformanceTimer
{
public:
    /*!
     * \brief Get singleton instance
     */
    static PerformanceTimer& instance()
    {
        static PerformanceTimer inst;
        return inst;
    }

    /*!
     * \brief Start timing an operation
     * \param name Unique identifier for the operation
     */
    void start(const QString& name)
    {
        QMutexLocker locker(&m_mutex);
        m_activeTimers[name].start();
    }

    /*!
     * \brief Stop timing an operation and accumulate the elapsed time
     * \param name Unique identifier for the operation
     */
    void stop(const QString& name)
    {
        QMutexLocker locker(&m_mutex);
        
        auto it = m_activeTimers.find(name);
        if (it != m_activeTimers.end()) {
            qint64 elapsed = it.value().nsecsElapsed();
            // Use remove() instead of erase() to avoid GCC 7.1 ABI warnings with iterators
            m_activeTimers.remove(name);
            
            // Accumulate timing data
            TimingData& data = m_timings[name];
            data.totalNsec += elapsed;
            data.count++;
            
            if (data.count == 1 || elapsed < data.minNsec) {
                data.minNsec = elapsed;
            }
            if (data.count == 1 || elapsed > data.maxNsec) {
                data.maxNsec = elapsed;
            }
        }
    }

    /*!
     * \brief Increment a counter
     * \param name Counter name
     * \param value Amount to increment (default 1)
     */
    void increment(const QString& name, qint64 value = 1)
    {
        QMutexLocker locker(&m_mutex);
        m_counters[name] += value;
    }

    /*!
     * \brief Report all accumulated timing data to debug log
     */
    void report()
    {
        QMutexLocker locker(&m_mutex);
        
        if (m_timings.isEmpty() && m_counters.isEmpty()) {
            qDebug() << "=== PERFORMANCE REPORT: No data collected ===";
            return;
        }
        
        qDebug() << "=================================================================";
        qDebug() << "=== PERFORMANCE REPORT ===";
        qDebug() << "=================================================================";
        
        // Calculate total time
        qint64 totalNsec = 0;
        for (const TimingData& data : m_timings) {
            totalNsec += data.totalNsec;
        }
        double totalSec = totalNsec / 1000000000.0;
        
        qDebug() << QString("Total Measured Time: %1 seconds").arg(totalSec, 0, 'f', 3);
        qDebug() << "";
        
        // Sort by total time (descending)
        QList<QPair<QString, TimingData>> sorted;
        for (auto it = m_timings.begin(); it != m_timings.end(); ++it) {
            sorted.append(qMakePair(it.key(), it.value()));
        }
        std::sort(sorted.begin(), sorted.end(), 
                  [](const QPair<QString, TimingData>& a, const QPair<QString, TimingData>& b) {
                      return a.second.totalNsec > b.second.totalNsec;
                  });
        
        // Print timing data
        qDebug() << "TIMING DATA (sorted by total time):";
        qDebug() << QString("%1 | %2 | %3 | %4 | %5 | %6")
                    .arg("Operation", -40)
                    .arg("Count", 8)
                    .arg("Total(s)", 10)
                    .arg("Avg(ms)", 10)
                    .arg("Min(ms)", 10)
                    .arg("Max(ms)", 10);
        qDebug() << QString("-").repeated(100);
        
        for (const auto& pair : sorted) {
            const QString& name = pair.first;
            const TimingData& data = pair.second;
            
            double totalMs = data.totalNsec / 1000000.0;
            double avgMs = (data.totalNsec / data.count) / 1000000.0;
            double minMs = data.minNsec / 1000000.0;
            double maxMs = data.maxNsec / 1000000.0;
            double percent = totalNsec > 0 ? (100.0 * data.totalNsec / totalNsec) : 0.0;
            
            qDebug() << QString("%1 | %2 | %3 | %4 | %5 | %6 | %7%")
                        .arg(name, -40)
                        .arg(data.count, 8)
                        .arg(totalMs / 1000.0, 10, 'f', 3)
                        .arg(avgMs, 10, 'f', 2)
                        .arg(minMs, 10, 'f', 2)
                        .arg(maxMs, 10, 'f', 2)
                        .arg(percent, 6, 'f', 1);
        }
        
        // Print counters
        if (!m_counters.isEmpty()) {
            qDebug() << "";
            qDebug() << "COUNTERS:";
            QMapIterator<QString, qint64> counterIt(m_counters);
            while (counterIt.hasNext()) {
                counterIt.next();
                qDebug() << QString("%1: %2").arg(counterIt.key(), -40).arg(counterIt.value());
            }
        }
        
        qDebug() << "=================================================================";
    }

    /*!
     * \brief Reset all timing data
     */
    void reset()
    {
        QMutexLocker locker(&m_mutex);
        m_timings.clear();
        m_activeTimers.clear();
        m_counters.clear();
    }

private:
    PerformanceTimer() {}
    ~PerformanceTimer() {}
    
    // Prevent copying
    PerformanceTimer(const PerformanceTimer&) = delete;
    PerformanceTimer& operator=(const PerformanceTimer&) = delete;

    struct TimingData {
        qint64 totalNsec = 0;
        qint64 minNsec = 0;
        qint64 maxNsec = 0;
        qint64 count = 0;
    };

    QMutex m_mutex;
    QMap<QString, QElapsedTimer> m_activeTimers;
    QMap<QString, TimingData> m_timings;
    QMap<QString, qint64> m_counters;
};

/*!
 * \class PerformanceScopeTimer
 * \brief RAII-style timer for automatic start/stop
 *
 * Usage: PerformanceScopeTimer timer("operation_name");
 * Timer starts on construction and stops on destruction.
 */
class PerformanceScopeTimer
{
public:
    explicit PerformanceScopeTimer(const QString& name) : m_name(name)
    {
        PerformanceTimer::instance().start(m_name);
    }
    
    ~PerformanceScopeTimer()
    {
        PerformanceTimer::instance().stop(m_name);
    }

private:
    QString m_name;
};

#endif // PERFORMANCE_TIMER_H
