/* ORF File I/O Implementation
 *
 * Copyright (c) 2026 The OSCAR Team
 *
 * This file implements the OrfFileIO class for reading and writing
 * OSCAR Report File (.orf) format files.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of the source code
 * for more details. */

#include "orf_file_io.h"
#include "report_tree_repository.h"
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QDebug>
#include <QSet>

bool OrfFileIO::readFile(const QString& filePath,
                         QList<OrfReportEntry>& entries,
                         QString& errorMessage)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        errorMessage = QString("Failed to open file: %1").arg(file.errorString());
        qCritical() << "OrfFileIO::readFile:" << errorMessage;
        return false;
    }
    
    QTextStream in(&file);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    in.setCodec("UTF-8");
#else
    in.setEncoding(QStringConverter::Utf8);
#endif
    
    entries.clear();
    QSet<QString> seenFolders;  // Track folders we've already added
    
    OrfReportEntry currentEntry;
    bool inHeredoc = false;
    QString heredocContent;
    int lineNumber = 0;
    
    while (!in.atEnd()) {
        QString line = in.readLine();
        lineNumber++;
        
        // Handle heredoc mode
        if (inHeredoc) {
            if (isHeredocEnd(line)) {
                // End of heredoc block
                currentEntry.query = heredocContent;
                entries.append(currentEntry);
                
                // Reset for next entry
                currentEntry = OrfReportEntry();
                heredocContent.clear();
                inHeredoc = false;
            } else {
                // Accumulate query lines
                if (!heredocContent.isEmpty()) {
                    heredocContent += "\n";
                }
                heredocContent += line;
            }
            continue;
        }
        
        // Skip blank lines and comments (outside heredoc)
        QString trimmed = line.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith("#")) {
            continue;
        }
        
        // Folder declaration: === Folder: <path> ===
        if (trimmed.startsWith("=== Folder:") && trimmed.endsWith("===")) {
            QString path = extractPath(trimmed, "=== Folder:");
            if (path.isEmpty()) {
                errorMessage = QString("Line %1: Invalid folder declaration").arg(lineNumber);
                return false;
            }
            
            // Only add if we haven't seen this folder yet
            if (!seenFolders.contains(path)) {
                OrfReportEntry folderEntry;
                folderEntry.path = path;
                folderEntry.name = getLeafName(path);
                folderEntry.isFolder = true;
                entries.append(folderEntry);
                seenFolders.insert(path);
                
                // Also ensure all parent folders are present
                QString parentPath = getFolderPath(path);
                while (!parentPath.isEmpty() && !seenFolders.contains(parentPath)) {
                    OrfReportEntry parentEntry;
                    parentEntry.path = parentPath;
                    parentEntry.name = getLeafName(parentPath);
                    parentEntry.isFolder = true;
                    entries.prepend(parentEntry);  // Add at beginning so hierarchy is correct
                    seenFolders.insert(parentPath);
                    parentPath = getFolderPath(parentPath);
                }
            }
            continue;
        }
        
        // Report declaration: === Report: <path/name> ===
        if (trimmed.startsWith("=== Report:") && trimmed.endsWith("===")) {
            QString path = extractPath(trimmed, "=== Report:");
            if (path.isEmpty()) {
                errorMessage = QString("Line %1: Invalid report declaration").arg(lineNumber);
                return false;
            }
            
            currentEntry.path = path;
            currentEntry.name = getLeafName(path);
            currentEntry.isFolder = false;
            
            // Ensure parent folder exists in entries
            QString parentPath = getFolderPath(path);
            if (!parentPath.isEmpty() && !seenFolders.contains(parentPath)) {
                // Add parent folder(s) recursively
                QStringList pathParts = parentPath.split("/", Qt::SkipEmptyParts);
                QString buildPath;
                for (const QString& part : pathParts) {
                    if (!buildPath.isEmpty()) buildPath += "/";
                    buildPath += part;
                    
                    if (!seenFolders.contains(buildPath)) {
                        OrfReportEntry folderEntry;
                        folderEntry.path = buildPath;
                        folderEntry.name = getLeafName(buildPath);
                        folderEntry.isFolder = true;
                        entries.append(folderEntry);
                        seenFolders.insert(buildPath);
                    }
                }
            }
            continue;
        }
        
        // Description line
        if (trimmed.startsWith("Description:")) {
            currentEntry.description = trimmed.mid(12).trimmed();  // Skip "Description:"
            continue;
        }
        
        // Query heredoc start
        if (trimmed.startsWith("Query:") && isHeredocStart(trimmed)) {
            inHeredoc = true;
            heredocContent.clear();
            continue;
        }
    }
    
    // Check for unterminated heredoc
    if (inHeredoc) {
        errorMessage = "Unexpected end of file: unterminated SQL heredoc block";
        qCritical() << "OrfFileIO::readFile:" << errorMessage;
        return false;
    }
    
    file.close();
    
    qDebug() << "OrfFileIO::readFile: Successfully parsed" << entries.count() 
             << "entries from" << filePath;
    return true;
}

bool OrfFileIO::writeFile(const QString& filePath,
                          const QList<OrfReportEntry>& entries,
                          const QString& source,
                          QString& errorMessage)
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        errorMessage = QString("Failed to create file: %1").arg(file.errorString());
        qCritical() << "OrfFileIO::writeFile:" << errorMessage;
        return false;
    }
    
    QTextStream out(&file);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    out.setCodec("UTF-8");
    out.setGenerateByteOrderMark(false);
#else
    out.setEncoding(QStringConverter::Utf8);
    out.setGenerateByteOrderMark(false);
#endif
    
    // Write header
    out << "# OSCAR Report File\n";
    out << "# Format: ORF 1.0\n";
    out << "# Exported: " << QDateTime::currentDateTime().toString(Qt::ISODate) << "\n";
    out << "# Source: " << source << "\n";
    out << "\n";
    
    // Track which folders we've written
    QSet<QString> writtenFolders;
    
    // Write entries
    for (const OrfReportEntry& entry : entries) {
        if (entry.isFolder) {
            // Only write folder declaration if not already written
            if (!writtenFolders.contains(entry.path)) {
                out << "=== Folder: " << entry.path << " ===\n";
                out << "\n";
                writtenFolders.insert(entry.path);
            }
        } else {
            // Ensure parent folder is declared first
            QString parentPath = getFolderPath(entry.path);
            if (!parentPath.isEmpty() && !writtenFolders.contains(parentPath)) {
                out << "=== Folder: " << parentPath << " ===\n";
                out << "\n";
                writtenFolders.insert(parentPath);
            }
            
            // Write report
            out << "=== Report: " << entry.path << " ===\n";
            
            if (!entry.description.isEmpty()) {
                out << "Description: " << entry.description << "\n";
            }
            
            out << "Query: <<SQL\n";
            out << entry.query;
            if (!entry.query.endsWith("\n")) {
                out << "\n";
            }
            out << "SQL\n";
            out << "\n";
        }
    }
    
    file.close();
    
    qDebug() << "OrfFileIO::writeFile: Successfully wrote" << entries.count() 
             << "entries to" << filePath;
    return true;
}

int OrfFileIO::importToDatabase(const QList<OrfReportEntry>& entries,
                                qint64 parentNodeId,
                                const QString& source,
                                QString& errorMessage)
{
    ReportTreeRepository repo;
    int reportCount = 0;
    
    // Map to track created folder IDs by path
    QMap<QString, qint64> folderMap;
    folderMap[""] = parentNodeId;  // Empty path maps to parent
    
    for (const OrfReportEntry& entry : entries) {
        if (entry.isFolder) {
            // Create folder
            QString parentPath = getFolderPath(entry.path);
            qint64 folderParentId = folderMap.value(parentPath, parentNodeId);
            
            // Check if folder already exists
            if (repo.exists(folderParentId, entry.name)) {
                // Folder exists - find its ID for reuse
                QList<ReportTreeNode> children = repo.findChildren(folderParentId);
                for (const ReportTreeNode& child : children) {
                    if (child.name == entry.name && child.nodeType == "folder") {
                        folderMap[entry.path] = child.id;
                        qDebug() << "OrfFileIO::importToDatabase: Reusing existing folder" 
                                 << entry.path;
                        break;
                    }
                }
            } else {
                // Create new folder
                ReportTreeNode folderNode;
                folderNode.parentId = folderParentId;
                folderNode.name = entry.name;
                folderNode.nodeType = "folder";
                folderNode.source = source;
                folderNode.displayOrder = 0;
                
                qint64 folderId = repo.create(folderNode);
                if (folderId == 0) {
                    errorMessage = QString("Failed to create folder: %1").arg(entry.path);
                    return -1;
                }
                
                folderMap[entry.path] = folderId;
                qDebug() << "OrfFileIO::importToDatabase: Created folder" << entry.path 
                         << "with ID" << folderId;
            }
        } else {
            // Create report
            QString parentPath = getFolderPath(entry.path);
            qint64 reportParentId = folderMap.value(parentPath, parentNodeId);
            
            // Check if report already exists
            if (repo.exists(reportParentId, entry.name)) {
                qWarning() << "OrfFileIO::importToDatabase: Report" << entry.name 
                          << "already exists, skipping";
                // TODO: Implement user choice (skip, overwrite, rename)
                continue;
            }
            
            // Create new report
            ReportTreeNode reportNode;
            reportNode.parentId = reportParentId;
            reportNode.name = entry.name;
            reportNode.nodeType = "report";
            reportNode.source = source;
            reportNode.description = entry.description;
            reportNode.query = entry.query;
            reportNode.displayOrder = 0;
            
            qint64 reportId = repo.create(reportNode);
            if (reportId == 0) {
                errorMessage = QString("Failed to create report: %1").arg(entry.path);
                return -1;
            }
            
            reportCount++;
            qDebug() << "OrfFileIO::importToDatabase: Created report" << entry.path 
                     << "with ID" << reportId;
        }
    }
    
    qDebug() << "OrfFileIO::importToDatabase: Successfully imported" << reportCount 
             << "reports";
    return reportCount;
}

QList<OrfReportEntry> OrfFileIO::exportFromDatabase(qint64 rootNodeId)
{
    QList<OrfReportEntry> entries;
    exportNodeRecursive(rootNodeId, "", entries);
    return entries;
}

// Helper methods

QString OrfFileIO::extractPath(const QString& line, const QString& prefix)
{
    QString trimmed = line.trimmed();
    if (!trimmed.startsWith(prefix) || !trimmed.endsWith("===")) {
        return QString();
    }
    
    // Extract text between prefix and closing ===
    int start = prefix.length();
    int end = trimmed.length() - 3;  // Remove " ==="
    QString path = trimmed.mid(start, end - start).trimmed();
    
    return path;
}

bool OrfFileIO::isHeredocStart(const QString& line)
{
    return line.trimmed().contains("<<SQL");
}

bool OrfFileIO::isHeredocEnd(const QString& line)
{
    return line.trimmed() == "SQL";
}

QString OrfFileIO::getFolderPath(const QString& fullPath)
{
    int lastSlash = fullPath.lastIndexOf('/');
    if (lastSlash < 0) {
        return QString();  // No parent folder
    }
    return fullPath.left(lastSlash);
}

QString OrfFileIO::getLeafName(const QString& fullPath)
{
    int lastSlash = fullPath.lastIndexOf('/');
    if (lastSlash < 0) {
        return fullPath;  // No slash, entire path is the name
    }
    return fullPath.mid(lastSlash + 1);
}

qint64 OrfFileIO::ensureFolderPath(const QString& path, qint64 parentNodeId, 
                                   const QString& source)
{
    if (path.isEmpty()) {
        return parentNodeId;
    }
    
    ReportTreeRepository repo;
    QStringList parts = path.split("/", Qt::SkipEmptyParts);
    qint64 currentParentId = parentNodeId;
    
    QString buildPath;
    for (const QString& part : parts) {
        if (!buildPath.isEmpty()) buildPath += "/";
        buildPath += part;
        
        // Check if folder exists
        if (repo.exists(currentParentId, part)) {
            // Find its ID
            QList<ReportTreeNode> children = repo.findChildren(currentParentId);
            for (const ReportTreeNode& child : children) {
                if (child.name == part && child.nodeType == "folder") {
                    currentParentId = child.id;
                    break;
                }
            }
        } else {
            // Create folder
            ReportTreeNode folderNode;
            folderNode.parentId = currentParentId;
            folderNode.name = part;
            folderNode.nodeType = "folder";
            folderNode.source = source;
            folderNode.displayOrder = 0;
            
            currentParentId = repo.create(folderNode);
            if (currentParentId == 0) {
                qCritical() << "OrfFileIO::ensureFolderPath: Failed to create folder" 
                           << part;
                return 0;
            }
        }
    }
    
    return currentParentId;
}

void OrfFileIO::exportNodeRecursive(qint64 nodeId, const QString& basePath,
                                    QList<OrfReportEntry>& entries)
{
    ReportTreeRepository repo;
    ReportTreeNode node = repo.findById(nodeId);
    
    if (node.id == 0) {
        qWarning() << "OrfFileIO::exportNodeRecursive: Node" << nodeId << "not found";
        return;
    }
    
    // Skip root nodes themselves
    if (node.nodeType == "root") {
        // Just export children
        QList<ReportTreeNode> children = repo.findChildrenOrdered(nodeId);
        for (const ReportTreeNode& child : children) {
            exportNodeRecursive(child.id, "", entries);
        }
        return;
    }
    
    // Build path for this node
    QString nodePath = basePath.isEmpty() ? node.name : basePath + "/" + node.name;
    
    if (node.nodeType == "folder") {
        // Add folder entry
        OrfReportEntry folderEntry;
        folderEntry.path = nodePath;
        folderEntry.name = node.name;
        folderEntry.isFolder = true;
        entries.append(folderEntry);
        
        // Recursively export children
        QList<ReportTreeNode> children = repo.findChildrenOrdered(nodeId);
        for (const ReportTreeNode& child : children) {
            exportNodeRecursive(child.id, nodePath, entries);
        }
    } else if (node.nodeType == "report") {
        // Add report entry
        OrfReportEntry reportEntry;
        reportEntry.path = nodePath;
        reportEntry.name = node.name;
        reportEntry.description = node.description;
        reportEntry.query = node.query;
        reportEntry.isFolder = false;
        entries.append(reportEntry);
    }
}
