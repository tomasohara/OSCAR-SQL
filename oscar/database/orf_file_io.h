/* ORF File I/O Header
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file contains the OrfFileIO class for reading and writing
 * OSCAR Report File (.orf) format files.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#ifndef ORF_FILE_IO_H
#define ORF_FILE_IO_H

#include <QString>
#include <QList>

/*!
 * \struct OrfReportEntry
 * \brief Data structure for a report entry from an ORF file
 *
 * Represents a single folder or report entry parsed from or to be written
 * to an .orf file.
 */
struct OrfReportEntry {
    QString path;           ///< Full path e.g. "Daily Summaries/by Day"
    QString name;           ///< Leaf name e.g. "by Day"
    QString description;    ///< Optional description
    QString query;          ///< SQL query template (empty for folders)
    bool isFolder;          ///< true for folder, false for report
    
    OrfReportEntry() : isFolder(false) {}
};

/*!
 * \class OrfFileIO
 * \brief Utility class for reading and writing .orf files
 *
 * Provides static methods to parse OSCAR Report Files (.orf) and write
 * report definitions to .orf format. The format uses heredoc-style SQL
 * blocks and path notation for hierarchical structure.
 */
class OrfFileIO
{
public:
    /*!
     * \brief Parse an .orf file into a list of entries
     * \param filePath Full path to the .orf file to read
     * \param entries Output list of parsed entries
     * \param errorMessage Output error message if parsing fails
     * \return true if successful, false on error
     *
     * Parses the file using the ORF 1.0 format specification. Entries
     * are returned in the order they appear in the file. Folder entries
     * are created for any folder declarations or implied by report paths.
     */
    static bool readFile(const QString& filePath,
                         QList<OrfReportEntry>& entries,
                         QString& errorMessage);
    
    /*!
     * \brief Write entries to an .orf file
     * \param filePath Full path to the .orf file to write
     * \param entries List of entries to write
     * \param source Source identifier ("System", "User", or "Mixed")
     * \param errorMessage Output error message if writing fails
     * \return true if successful, false on error
     *
     * Writes the entries in ORF 1.0 format with UTF-8 encoding and Unix
     * line endings (\n) for cross-platform compatibility.
     */
    static bool writeFile(const QString& filePath,
                          const QList<OrfReportEntry>& entries,
                          const QString& source,
                          QString& errorMessage);
    
    /*!
     * \brief Import entries into database under a given parent node
     * \param entries List of entries to import
     * \param parentNodeId Database ID of parent node to import under
     * \param source Source type for imported nodes ("system" or "user")
     * \param errorMessage Output error message if import fails
     * \return Number of reports imported, or -1 on error
     *
     * Creates folders and reports in the database tree. If a folder with
     * the same name already exists, it is reused (merge behavior). If a
     * report with the same name exists, the behavior depends on user choice
     * (skip, overwrite, or rename).
     */
    static int importToDatabase(const QList<OrfReportEntry>& entries,
                                qint64 parentNodeId,
                                const QString& source,
                                QString& errorMessage);
    
    /*!
     * \brief Export a subtree from database to entry list
     * \param rootNodeId Database ID of root node to export
     * \return List of entries representing the subtree
     *
     * Recursively exports all folders and reports under the given node.
     * The returned entries are suitable for writing to an .orf file.
     */
    static QList<OrfReportEntry> exportFromDatabase(qint64 rootNodeId);

private:
    // Helper methods for parsing
    static QString extractPath(const QString& line, const QString& prefix);
    static bool isHeredocStart(const QString& line);
    static bool isHeredocEnd(const QString& line);
    static QString getFolderPath(const QString& fullPath);
    static QString getLeafName(const QString& fullPath);
    
    // Helper methods for database operations
    static qint64 ensureFolderPath(const QString& path, qint64 parentNodeId, 
                                   const QString& source);
    static void exportNodeRecursive(qint64 nodeId, const QString& basePath,
                                    QList<OrfReportEntry>& entries);
};

#endif // ORF_FILE_IO_H
