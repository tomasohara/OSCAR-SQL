/* Viatom Unit Tests
 *
 * Copyright (c) 2020-2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "viatomtests.h"
#include "sessiontests.h"

#define TESTDATA_PATH "./testdata/"

static ViatomLoader* s_loader = nullptr;
static QString viatomOutputPath(const QString & inpath, const QString & suffix);

void ViatomTests::initTestCase(void)
{
    p_profile = new Profile(TESTDATA_PATH "profile/", false);

    schema::init();
    ViatomLoader::Register();
    s_loader = dynamic_cast<ViatomLoader*>(lookupLoader(viatom_class_name));
}

void ViatomTests::cleanupTestCase(void)
{
    delete p_profile;
    p_profile = nullptr;
}


// ====================================================================================================

static void parseAndEmitSessionYaml(const QString & path)
{
    qDebug() << path;

    Session* session = s_loader->ParseFile(path);
    if (session != nullptr) {
        QString outpath = viatomOutputPath(path, "-session.yml");
        SessionToYaml(outpath, session, true);
        delete session;
    }
}

void ViatomTests::testSessionsToYaml()
{
    static const QString root_path = TESTDATA_PATH "viatom/input/";

    QDir root(root_path);
    root.setFilter(QDir::NoDotAndDotDot | QDir::Dirs);
    root.setSorting(QDir::Name);
    for (auto & dir_info : root.entryInfoList()) {
        QDir dir(dir_info.canonicalFilePath());
        dir.setFilter(QDir::Files | QDir::Hidden);
        dir.setNameFilters(s_loader->getNameFilter());
        dir.setSorting(QDir::Name);
        for (auto & fi : dir.entryInfoList()) {
            parseAndEmitSessionYaml(fi.canonicalFilePath());
        }
    }
}


// ====================================================================================================

QString viatomOutputPath(const QString & inpath, const QString & suffix)
{
    // Output to viatom/output/DIR/FILENAME(-session.yml, etc.)
    QFileInfo path(inpath);
    QString basename = path.fileName();
    QString foldername = path.dir().dirName();

    QDir outdir(TESTDATA_PATH "viatom/output/" + foldername);
    outdir.mkpath(".");
    
    QString filename = QString("%1%2")
                        .arg(basename)
                        .arg(suffix);
    return outdir.path() + QDir::separator() + filename;
}
