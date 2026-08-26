/* SleepLib Apple Health Loader Implementation
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include <QDebug>
#include <QFile>
#include <QFileInfo>

#include "applehealth_loader.h"

AppleHealthLoader::AppleHealthLoader()
{
    m_type = MT_OXIMETER;
}

AppleHealthLoader::~AppleHealthLoader()
{
}

bool AppleHealthLoader::Detect(const QString & path)
{
    Q_UNUSED(path);
    return false;
}

int AppleHealthLoader::OpenFile(const QString & filename)
{
    if (filename.endsWith(".zip", Qt::CaseInsensitive)) {
        qDebug() << "AppleHealthLoader::OpenFile:" << filename << "is a ZIP archive; extract export.xml first";
        return -1;
    }

    QFileInfo fileInfo(filename);
    if (!fileInfo.exists() || !fileInfo.isFile()) {
        qDebug() << "AppleHealthLoader::OpenFile: file does not exist:" << filename;
        return -1;
    }

    QFile file(filename);
    if (!file.open(QIODevice::ReadOnly)) {
        qDebug() << "AppleHealthLoader::OpenFile: could not open:" << filename;
        return -1;
    }

    const QByteArray prefix = file.read(8192);
    if (!prefix.contains("HealthData") && !prefix.contains("HealthKit Export")) {
        qDebug() << "AppleHealthLoader::OpenFile: file does not look like an Apple Health export:" << filename;
        return -1;
    }

    AppleHealthParser parser;
    // cutoff wired to the profile CPAP range in a later stage
    if (!parser.parse(filename, m_data)) {
        qWarning() << "AppleHealthLoader::OpenFile:" << parser.errorString();
        return -1;
    }

    if (!m_data.sleepStages.isEmpty()) {
        qDebug() << "AppleHealthLoader::OpenFile: stages:" << m_data.sleepStages.size();
    }
    if (!m_data.heartRate.isEmpty()) {
        qDebug() << "AppleHealthLoader::OpenFile: hr:" << m_data.heartRate.size();
    }
    if (!m_data.spo2.isEmpty()) {
        qDebug() << "AppleHealthLoader::OpenFile: spo2:" << m_data.spo2.size();
    }
    if (!m_data.respRate.isEmpty()) {
        qDebug() << "AppleHealthLoader::OpenFile: resp:" << m_data.respRate.size();
    }
    if (!m_data.hrv.isEmpty()) {
        qDebug() << "AppleHealthLoader::OpenFile: hrv:" << m_data.hrv.size();
    }
    if (!m_data.breathingDisturbances.isEmpty()) {
        qDebug() << "AppleHealthLoader::OpenFile: bd:"
                 << m_data.breathingDisturbances.size();
    }
    if (!m_data.wristTemp.isEmpty()) {
        qDebug() << "AppleHealthLoader::OpenFile: wristTemp:" << m_data.wristTemp.size();
    }
    if (!m_data.weights.isEmpty()) {
        qDebug() << "AppleHealthLoader::OpenFile: weights:" << m_data.weights.size();
    }
    qDebug() << "AppleHealthLoader::OpenFile: recordsSeen:" << m_data.recordsSeen;
    qDebug() << "AppleHealthLoader::OpenFile: sleepSourceCounts:" << m_data.sleepSourceCounts;
    return 0;
}

static bool applehealth_initialized = false;

void AppleHealthLoader::Register()
{
    if (applehealth_initialized) { return; }

    qDebug("Registering AppleHealthLoader");
    RegisterLoader(new AppleHealthLoader());
    applehealth_initialized = true;
}
