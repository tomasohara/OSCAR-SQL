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

    qDebug() << "AppleHealthLoader::OpenFile: recognized Apple Health export:" << filename;
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
