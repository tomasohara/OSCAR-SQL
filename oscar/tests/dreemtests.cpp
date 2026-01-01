/* Dreem Unit Tests
 *
 * Copyright (c) 2020-2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "dreemtests.h"
#include "sessiontests.h"

#define TESTDATA_PATH "./testdata/"

static DreemLoader* s_loader = nullptr;
static QString dreemOutputPath(const QString & inpath, int sid, const QString & suffix);

void DreemTests::initTestCase(void)
{
    p_profile = new Profile(TESTDATA_PATH "profile/", false);

    schema::init();
    DreemLoader::Register();
    s_loader = dynamic_cast<DreemLoader*>(lookupLoader(dreem_class_name));
}

void DreemTests::cleanupTestCase(void)
{
    delete p_profile;
    p_profile = nullptr;
}


// ====================================================================================================

static void parseAndEmitSessionYaml(const QString & path)
{
    qDebug() << path;

    if (s_loader->openCSV(path)) {
        int count = 0;
        Session* session;
        while ((session = s_loader->readNextSession()) != nullptr) {
            QString outpath = dreemOutputPath(path, session->session(), "-session.yml");
            SessionToYaml(outpath, session, true);
            delete session;
            count++;
        }
        if (count == 0) {
            qWarning() << "no sessions found";
        }
        s_loader->closeCSV();
    } else {
        qWarning() << "unable to open file";
    }
}

void DreemTests::testSessionsToYaml()
{
    static const QString root_path = TESTDATA_PATH "dreem/input/";

    QDir root(root_path);
    root.setFilter(QDir::NoDotAndDotDot | QDir::Dirs);
    root.setSorting(QDir::Name);
    for (auto & dir_info : root.entryInfoList()) {
        QDir dir(dir_info.canonicalFilePath());
        dir.setFilter(QDir::Files | QDir::Hidden);
        dir.setNameFilters(QStringList("*.csv"));
        dir.setSorting(QDir::Name);
        for (auto & fi : dir.entryInfoList()) {
            parseAndEmitSessionYaml(fi.canonicalFilePath());
        }
    }
}


// ====================================================================================================

QString dreemOutputPath(const QString & inpath, int sid, const QString & suffix)
{
    // Output to dreem/output/DIR/FILENAME(-session.yml, etc.)
    QFileInfo path(inpath);
    QString basename = path.baseName();
    QString foldername = path.dir().dirName();

    QDir outdir(TESTDATA_PATH "dreem/output/" + foldername);
    outdir.mkpath(".");
    
    QString filename = QString("%1-%2%3")
                        .arg(basename)
                        .arg(sid, 8, 10, QChar('0'))
                        .arg(suffix);
    return outdir.path() + QDir::separator() + filename;
}
