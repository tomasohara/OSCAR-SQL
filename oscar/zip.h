/* OSCAR ZIP archive creation
 * Provides a Qt-convenient wrapper around miniz, see https://github.com/richgel999/miniz
 *
 * Copyright (c) 2020-2026 The OSCAR Team
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include <QObject>
#include <QString>
#include <QDir>
#include <QFile>

class ProgressDialog;

class ZipFile : public QObject
{
    Q_OBJECT

public:
    ZipFile();
    virtual ~ZipFile();
    
    bool Open(const QString & filepath);
    bool AddDirectory(const QString & path, ProgressDialog* progress=nullptr);  // add a directory and recurse
    bool AddDirectory(const QString & path, const QString & archive_name, ProgressDialog* progress=nullptr);  // add a directory and recurse
    bool AddFiles(class FileQueue & queue, ProgressDialog* progress=nullptr);  // add a fixed list of files
    bool AddFile(const QString & path, const QString & archive_name);  // add a single file
    void Close();
    
    bool aborted() const { return m_abort; }

public slots:
    void abort() { m_abort = true; }

signals:
    void setProgressMax(int max);
    void setProgressValue(int val);

protected:
    void* m_ctx;
    QFile m_file;
    bool m_abort;
    quint64 m_progress;
};


/*!
 * \class UnzipFile
 * \brief Extracts files from a ZIP archive using miniz.
 *
 * Mirrors the ZipFile interface for reading.  Files are extracted to memory
 * and written via QFile so that Unicode paths are handled correctly on all
 * platforms.
 *
 * Typical usage:
 * \code
 * UnzipFile zip;
 * if (zip.Open("/path/to/file.oscar")) {
 *     zip.ExtractAll("/tmp/extracted");
 *     zip.Close();
 * }
 * \endcode
 */
class UnzipFile : public QObject
{
    Q_OBJECT

public:
    UnzipFile();
    virtual ~UnzipFile();

    /*!
     * \brief Open a ZIP archive for reading.
     * \param filepath  Absolute path to the ZIP / .oscar file.
     * \return true on success.
     */
    bool Open(const QString& filepath);

    /*!
     * \brief Extract all entries to \a destDir.
     *
     * Directory entries are created automatically.  Existing files are
     * overwritten.
     *
     * \param destDir  Destination root directory (created if absent).
     * \return true on success.
     */
    bool ExtractAll(const QString& destDir);

    /*!
     * \brief Close the archive and free internal resources.
     */
    void Close();

    /*!
     * \brief Return the number of entries (files + directories) in the archive.
     *
     * Only valid after a successful Open().
     */
    int entryCount() const;

protected:
    void*      m_ctx;      ///< Heap-allocated mz_zip_archive.
    bool       m_open;     ///< True when the archive has been opened successfully.
    QByteArray m_fileData; ///< File contents kept alive for mz_zip_reader_init_mem.
};


class FileQueue
{
    struct Entry
    {
        QString path;
        QString name;
    };
    QList<Entry> m_files;
    int m_dir_count;
    int m_file_count;
    quint64 m_byte_count;

public:
    FileQueue() : m_dir_count(0), m_file_count(0), m_byte_count(0) {}
    ~FileQueue() = default;

    //!brief Remove a file from the queue, return the number of instances removed.
    int Remove(const QString & path, QString* outName=nullptr);
    
    //!brief Recursively add a directory and its contents to the queue along with the prefix to be used in an archive.
    bool AddDirectory(const QString & path, const QString & prefix="");
    
    //!brief Add a file to the queue along with the name to be used in an archive.
    bool AddFile(const QString & path, const QString & archive_name="");

    inline int dirCount() const { return m_dir_count; }
    inline int fileCount() const { return m_file_count; }
    inline quint64 byteCount() const { return m_byte_count; }
    const QList<Entry> & files() const { return m_files; }
    const QString toString() const;
};
