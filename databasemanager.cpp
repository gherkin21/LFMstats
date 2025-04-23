#include "databasemanager.h"
#include <QDir>
#include <QFile>
#include <QSaveFile>    // For atomic writes
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QtConcurrent> // Required for run()
#include <QStandardPaths>
#include <QDebug>
#include <QMetaObject>
#include <QFileInfo>
#include <algorithm>    // For std::sort
#include <QCoreApplication> // For applicationDirPath fallback
#include <QMap>             // Required for saveChunkSync grouping
#include <QThread>          // For sleep

DatabaseManager::DatabaseManager(const QString &basePath, QObject *parent)
    : QObject(parent),
    m_saveTaskRunning(false) // Initialize flag to false
{
    // Ensure base directory exists - Use absolute path
    QString absoluteBasePath = QDir::isAbsolutePath(basePath) ? basePath : QDir::cleanPath(QCoreApplication::applicationDirPath() + "/" + basePath);
    m_basePath = absoluteBasePath;
    qInfo() << "Database base path set to:" << m_basePath;
    QDir dir;
    if (!dir.mkpath(m_basePath)) {
        qCritical() << "Could not create base database directory:" << m_basePath;
        emit statusMessage("Error: Cannot create DB directory: " + m_basePath);
    }

    // Connect load watcher
    connect(&m_loadWatcher, &QFutureWatcherBase::finished, this, &DatabaseManager::handleLoadFinished);
}

// Destructor is now simpler (default or empty is fine as watchers are members)

// --- Async Save Operation ---
// Adds item to queue and ensures the processing task is running.
void DatabaseManager::saveScrobblesAsync(int pageNumber, const QString &username, const QList<ScrobbleData> &scrobbles) {
    if (username.isEmpty()){
        emit pageSaveFailed(pageNumber, "Cannot save data for empty username.");
        qWarning() << "[DB Manager] Save requested with empty username for page" << pageNumber;
        return;
    }
    if (scrobbles.isEmpty()){
        qDebug() << "[DB Manager] Skipping save request for empty scrobble list (page" << pageNumber << ")";
        // If MainWindow relies on completion signal even for empty pages during initial fetch,
        // we might need to emit pageSaveCompleted(pageNumber) here directly.
        // Let's assume MainWindow handles the empty page logic for now.
        return;
    }

    SaveWorkItem item;
    item.pageNumber = pageNumber;
    item.username = username;
    item.data = scrobbles; // Make a copy of the data for the queue item

    qDebug() << "[DB Manager] Adding save request for page" << pageNumber << "to queue.";
    { // Lock scope for queue access
        QMutexLocker locker(&m_saveQueueMutex);
        m_saveQueue.enqueue(item);
    }

    // Ensure a background task is running to process the queue
    startSaveTaskIfNotRunning();
}

// Checks if task is running; starts it via QtConcurrent::run if not.
void DatabaseManager::startSaveTaskIfNotRunning() {
    // Use atomic test-and-set: Try to change 'false' to 'true'.
    // If it was 'false' originally (meaning task wasn't running), it returns true and sets the flag.
    // If it was already 'true', it returns false and the flag remains true.
    if (m_saveTaskRunning.testAndSetAcquire(false, true)) {
        qInfo() << "[DB Manager] Starting background save task loop (QtConcurrent)...";
        // Launch the saveTaskLoop function in a separate thread from the global pool
        // Capture 'this' to call member function and access members
        QtConcurrent::run([this]() {
            this->saveTaskLoop();
        });
    } else {
        qDebug() << "[DB Manager] Save task already running.";
    }
}

// The function executed by QtConcurrent::run to process the save queue.
// This runs in a background thread from the Qt thread pool.
void DatabaseManager::saveTaskLoop() {
    qInfo() << "[DB Save Task] Started processing queue in thread" << QThread::currentThreadId();
    bool processedItemThisLoop = false; // Track if we processed anything

    forever { // Keep processing until queue is empty
        SaveWorkItem item;
        bool itemDequeued = false;

        // --- Check Queue Safely (Locked Scope) ---
        {
            QMutexLocker locker(&m_saveQueueMutex);
            if (!m_saveQueue.isEmpty()) {
                item = m_saveQueue.dequeue(); // Get next item
                itemDequeued = true;
                qInfo() << "[DB Save Task] Dequeued save request for page" << item.pageNumber << ". Items left:" << m_saveQueue.size();
            } else {
                // Queue is empty, the task for this run is finished.
                qInfo() << "[DB Save Task] Queue is empty. Finishing task loop.";
                // IMPORTANT: Release the running flag *before* exiting the loop.
                // Use storeRelease for memory synchronization.
                m_saveTaskRunning.storeRelease(false);
                break; // Exit the forever loop
            }
        } // --- Mutex Unlocked ---

        // --- Process Dequeued Item (Outside Lock) ---
        if (itemDequeued) {
            processedItemThisLoop = true; // Mark that we processed an item
            emit statusMessage(QString("Saving page %1...").arg(item.pageNumber));
            QString errorMsg;
            qDebug() << "[DB Save Task] >>> Calling saveChunkSync for page" << item.pageNumber << "...";
            bool success = saveChunkSync(m_basePath, item.username, item.data, errorMsg); // Call static sync function
            qDebug() << "[DB Save Task] <<< saveChunkSync returned:" << success << "for page" << item.pageNumber << "Error:" << errorMsg;

            // Emit result signals (these will be queued for the main thread)
            if (success) {
                emit pageSaveCompleted(item.pageNumber);
            } else {
                emit pageSaveFailed(item.pageNumber, errorMsg);
            }
            // Update status briefly (might be overwritten quickly)
            emit statusMessage(QString("Idle. (Last save: Page %1 %2)").arg(item.pageNumber).arg(success ? "OK" : "Failed"));

            // Optional short sleep to prevent this task from hogging CPU if queue refills instantly
            // QThread::msleep(10); // e.g., 10 milliseconds
        }
    } // End forever loop

    qInfo() << "[DB Save Task] Exiting save task loop function in thread" << QThread::currentThreadId();
    // Note: m_saveTaskRunning flag was set to false inside the loop before breaking.

    // --- Check if new items were added while processing the last item ---
    // If the queue is NOT empty now, it means new items arrived after the check inside the loop
    // but before the flag was set to false. We need to restart the task loop.
    // Requires locking the mutex again.
    {
        QMutexLocker locker(&m_saveQueueMutex);
        if (!m_saveQueue.isEmpty()) {
            qWarning() << "[DB Save Task] Queue is not empty after loop exit! Restarting task...";
            // Try to immediately restart the task loop if needed
            startSaveTaskIfNotRunning();
        } else {
            qDebug() << "[DB Save Task] Confirmed queue empty on loop exit.";
            // Ensure flag is false if we somehow missed it? Should be set already.
            // m_saveTaskRunning.storeRelease(false);
        }
    }
}


// --- Save State Check ---
// Checks if the save task is potentially active or if items are pending.
bool DatabaseManager::isSaveInProgress() const {
    // Check the atomic flag indicating if the save task loop *might* be active.
    bool taskRunning = m_saveTaskRunning.loadAcquire();
    // Also check the queue size, as the task might finish just before this check.
    int queueSize = 0;
    { // Need mutex to check queue size safely
        QMutexLocker locker(&m_saveQueueMutex);
        queueSize = m_saveQueue.size();
    }
    // Considered in progress if the task is running OR if items are still in the queue
    return taskRunning || (queueSize > 0);
}

// --- Load Operations & Sync Functions ---
// Load operations still use QFutureWatcher and separate QtConcurrent::run calls.
// Sync functions are static helpers used by both save and load tasks.

void DatabaseManager::loadScrobblesAsync(const QString &username, const QDateTime &from, const QDateTime &to) {
    if (m_loadWatcher.isRunning()) {
        emit loadError("Load operation (range) already in progress.");
        return;
    }
    if (username.isEmpty()){
        emit loadError("Cannot load data for empty username.");
        return;
    }
    emit statusMessage("Loading scrobbles for range...");
    m_lastLoadError.clear();
    QString basePath = m_basePath;
    QFuture<QList<ScrobbleData>> future = QtConcurrent::run([=]() mutable {
        return loadScrobblesSync(basePath, username, from, to, m_lastLoadError);
    });
    m_loadWatcher.setFuture(future);
}

void DatabaseManager::loadAllScrobblesAsync(const QString &username) {
    if (m_loadWatcher.isRunning()) {
        emit loadError("Load operation (all) already in progress.");
        return;
    }
    if (username.isEmpty()){
        emit loadError("Cannot load data for empty username.");
        return;
    }
    emit statusMessage("Loading all scrobbles...");
    m_lastLoadError.clear();
    QString basePath = m_basePath;
    QFuture<QList<ScrobbleData>> future = QtConcurrent::run([=]() mutable {
        return loadAllScrobblesSync(basePath, username, m_lastLoadError);
    });
    m_loadWatcher.setFuture(future);
}

qint64 DatabaseManager::getLastSyncTimestamp(const QString &username) {
    if (username.isEmpty()){
        qWarning() << "Cannot get last sync timestamp for empty username.";
        return 0;
    }
    emit statusMessage("Checking last sync time...");
    qint64 timestamp = findLastTimestampSync(m_basePath, username);
    return timestamp;
}

// Slot called when load future finishes
void DatabaseManager::handleLoadFinished() {
    QList<ScrobbleData> results = m_loadWatcher.result();
    emit statusMessage("Idle."); // Set status to idle after load attempt
    if (m_lastLoadError.isEmpty()) {
        emit loadComplete(results);
        qInfo() << "Database load via QtConcurrent finished successfully. Items:" << results.count();
    } else {
        emit loadError(m_lastLoadError);
        qWarning() << "Database load via QtConcurrent finished with errors:" << m_lastLoadError;
    }
    m_lastLoadError.clear(); // Reset error for next load
}


// --- Static Sync Functions Implementation ---

// Saves a specific chunk of scrobbles to the appropriate weekly files.
bool DatabaseManager::saveChunkSync(const QString &basePath, const QString &username, const QList<ScrobbleData> &scrobbles, QString &errorMsg) {
    qDebug() << "[DB Sync Save] Entered saveChunkSync for user" << username << "Scrobble Count:" << scrobbles.count();
    errorMsg.clear(); // Ensure error message is clear at start

    if (username.isEmpty()) { errorMsg = "Username cannot be empty."; qWarning() << "[DB Sync Save] " << errorMsg; return false; }
    if (scrobbles.isEmpty()) { qDebug() << "[DB Sync Save] Received empty scrobble list, skipping save."; return true; }

    QString userPath = basePath + "/" + username;
    QDir dir(userPath);
    qDebug() << "[DB Sync Save] Target user path:" << userPath;

    if (!dir.exists()) {
        qDebug() << "[DB Sync Save] User path does not exist, attempting to create.";
        if (!QDir().mkpath(userPath)) {
            errorMsg = "Could not create user directory: " + userPath;
            qCritical() << "[DB Sync Save] " << errorMsg;
            return false;
        }
        qDebug() << "[DB Sync Save] Successfully created user path.";
    }

    // Group scrobbles by target file
    QMap<QString, QList<ScrobbleData>> scrobblesByFile;
    for (const ScrobbleData &scrobble : scrobbles) {
        QString filePath = getWeekFilePath(userPath, scrobble.timestamp);
        scrobblesByFile[filePath].append(scrobble);
    }
    qDebug() << "[DB Sync Save] Grouped scrobbles into" << scrobblesByFile.count() << "target files.";

    bool all_ok = true;
    QString cumulativeErrors; // Accumulate errors if multiple files fail

    // Process each file
    for (auto it = scrobblesByFile.constBegin(); it != scrobblesByFile.constEnd(); ++it) {
        const QString &filePath = it.key();
        const QList<ScrobbleData> &newScrobblesForFile = it.value();
        QString currentFileError; // Error specific to this file iteration
        qDebug() << "[DB Sync Save] Processing file:" << QFileInfo(filePath).fileName() << "with" << newScrobblesForFile.count() << "new entries.";

        // 1. Read existing data
        QList<ScrobbleData> existingScrobbles;
        QMap<qint64, bool> existingTimestamps;
        QFile readFile(filePath);
        if (readFile.exists()) {
            if (readFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
                QByteArray data = readFile.readAll();
                readFile.close();
                QJsonParseError parseError;
                QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
                if (parseError.error == QJsonParseError::NoError && doc.isArray()) {
                    QJsonArray array = doc.array();
                    for (const QJsonValue &val : array) {
                        QJsonObject obj = val.toObject();
                        if(obj.contains("uts") && obj.contains("artist") && obj.contains("track")){
                            ScrobbleData s;
                            s.timestamp = QDateTime::fromSecsSinceEpoch(obj["uts"].toInteger(), Qt::UTC);
                            if (s.timestamp.isValid() && obj["uts"].toInteger() > 0) {
                                s.artist = obj["artist"].toString();
                                s.track = obj["track"].toString();
                                s.album = obj["album"].toString();
                                existingScrobbles.append(s);
                                existingTimestamps[s.timestamp.toSecsSinceEpoch()] = true;
                            }
                        }
                    }
                    qDebug() << "[DB Sync Save] Read" << existingScrobbles.count() << "valid existing entries from" << QFileInfo(filePath).fileName();
                } else {
                    qWarning() << "[DB Sync Save] File exists but is corrupt/not array:" << QFileInfo(filePath).fileName() << ". Overwriting.";
                    existingScrobbles.clear(); existingTimestamps.clear();
                }
            } else {
                currentFileError = "Could not open existing file for reading: " + QFileInfo(filePath).fileName() + " Error: " + readFile.errorString();
                qWarning() << "[DB Sync Save] " << currentFileError;
                all_ok = false; cumulativeErrors += currentFileError + "; ";
                continue; // Skip this file
            }
        }

        // 2. Merge new scrobbles
        int addedCount = 0;
        for (const ScrobbleData &newScrobble : newScrobblesForFile) {
            if (newScrobble.timestamp.isValid() && newScrobble.timestamp.toSecsSinceEpoch() > 0) {
                if (!existingTimestamps.contains(newScrobble.timestamp.toSecsSinceEpoch())) {
                    existingScrobbles.append(newScrobble); addedCount++;
                }
            }
        }
        if (addedCount == 0) { qDebug() << "[DB Sync Save] No unique entries to add for" << QFileInfo(filePath).fileName() << ". Skipping write."; continue; }
        qDebug() << "[DB Sync Save] Added" << addedCount << "unique entries. Total for file now:" << existingScrobbles.count();

        // 3. Sort
        std::sort(existingScrobbles.begin(), existingScrobbles.end(), [](const ScrobbleData &a, const ScrobbleData &b) { return a.timestamp < b.timestamp; });

        // 4. Write back using QSaveFile
        QJsonArray outputArray;
        for (const ScrobbleData &s : existingScrobbles) { /* ... populate array ... */
            QJsonObject obj;
            obj["artist"] = s.artist; obj["track"] = s.track; obj["album"] = s.album; obj["uts"] = s.timestamp.toSecsSinceEpoch();
            outputArray.append(obj);
        }
        QJsonDocument outputDoc(outputArray);
        QSaveFile saveFile(filePath);
        if (saveFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
            saveFile.write(outputDoc.toJson(QJsonDocument::Compact));
            if (!saveFile.commit()) {
                currentFileError = "Failed to commit changes to file: " + QFileInfo(filePath).fileName() + " Error: " + saveFile.errorString();
                qCritical() << "[DB Sync Save] COMMIT FAILED:" << currentFileError;
                all_ok = false; cumulativeErrors += currentFileError + "; ";
            } else { qDebug() << "[DB Sync Save] Successfully committed" << QFileInfo(filePath).fileName(); }
        } else {
            currentFileError = "Could not open QSaveFile for writing: " + QFileInfo(filePath).fileName() + " Error: " + saveFile.errorString();
            qWarning() << "[DB Sync Save] QSaveFile open failed:" << currentFileError;
            all_ok = false; cumulativeErrors += currentFileError + "; ";
        }
    } // End loop through target files

    if (!all_ok) {
        errorMsg = cumulativeErrors; // Set output error message if any file failed
    }
    return all_ok; // Return overall success status
}

// --- Static Helper Functions ---
QDateTime DatabaseManager::getWeekStart(const QDateTime timestamp) {
    QDateTime utcTimestamp = timestamp.toUTC();
    int daysToSubtract = utcTimestamp.date().dayOfWeek() - 1; // Monday = 1
    if (daysToSubtract < 0) daysToSubtract = 6; // Sunday = 7 -> 6
    return utcTimestamp.date().addDays(-daysToSubtract).startOfDay(Qt::UTC);
}

QString DatabaseManager::getWeekFilePath(const QString &userPath, const QDateTime timestamp) {
    QDateTime weekStart = getWeekStart(timestamp);
    return QString("%1/%2.json").arg(userPath).arg(weekStart.toSecsSinceEpoch());
}

// --- Load Sync Functions (Implementations are the same as previously shown) ---
QList<ScrobbleData> DatabaseManager::loadScrobblesSync(const QString &basePath, const QString &username, const QDateTime &from, const QDateTime &to, QString &errorMsg) {
    // ... Identical implementation to File 4 in the previous response ...
    // Includes directory check, file iteration, range check, JSON parsing, error accumulation
    qDebug() << "[Load Worker] Loading scrobbles for" << username << "from" << from.toString(Qt::ISODate) << "to" << to.toString(Qt::ISODate);
    QList<ScrobbleData> loadedScrobbles;
    QString userPath = basePath + "/" + username;
    QDir userDir(userPath);
    if (!userDir.exists()) { return loadedScrobbles; }
    userDir.setFilter(QDir::Files | QDir::NoDotAndDotDot); userDir.setNameFilters({"*.json"}); userDir.setSorting(QDir::Name);
    QStringList fileList = userDir.entryList();
    for (const QString &fileName : fileList) {
        qint64 fileTimestamp = fileName.chopped(5).toLongLong();
        if (fileTimestamp == 0 && fileName != "0.json") continue;
        QDateTime fileWeekStart = QDateTime::fromSecsSinceEpoch(fileTimestamp, Qt::UTC);
        QDateTime fileWeekEnd = fileWeekStart.addDays(7);
        if (fileWeekEnd <= from || fileWeekStart >= to) continue;
        QFile file(userDir.filePath(fileName));
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QByteArray data = file.readAll(); file.close();
            QJsonParseError parseError; QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
            if (parseError.error == QJsonParseError::NoError && doc.isArray()) {
                QJsonArray array = doc.array();
                for (const QJsonValue &val : array) {
                    QJsonObject obj = val.toObject();
                    if (obj.contains("uts") && obj.contains("artist") && obj.contains("track")) {
                        ScrobbleData s; qint64 uts = obj["uts"].toInteger(); s.timestamp = QDateTime::fromSecsSinceEpoch(uts, Qt::UTC);
                        if (s.timestamp.isValid() && uts > 0 && s.timestamp >= from && s.timestamp < to) {
                            s.artist = obj["artist"].toString(); s.track = obj["track"].toString(); s.album = obj["album"].toString();
                            loadedScrobbles.append(s);
                        }
                    }
                }
            } else { errorMsg += "Corrupt file: " + fileName + "; "; }
        } else { errorMsg += "Cannot read file: " + fileName + "; "; }
    }
    std::sort(loadedScrobbles.begin(), loadedScrobbles.end(), [](const ScrobbleData &a, const ScrobbleData &b) { return a.timestamp < b.timestamp; });
    return loadedScrobbles;
}
QList<ScrobbleData> DatabaseManager::loadAllScrobblesSync(const QString &basePath, const QString &username, QString &errorMsg) {
    QDateTime distantPast = QDateTime::fromSecsSinceEpoch(0, Qt::UTC);
    QDateTime distantFuture = QDateTime::currentDateTimeUtc().addYears(10);
    return loadScrobblesSync(basePath, username, distantPast, distantFuture, errorMsg);
}
qint64 DatabaseManager::findLastTimestampSync(const QString& basePath, const QString& username) {
    // ... Identical implementation to File 4 in the previous response ...
    // Includes directory check, reversed file iteration, JSON parsing, finding last 'uts'
    QString userPath = basePath + "/" + username; QDir userDir(userPath); if (!userDir.exists()) return 0;
    userDir.setFilter(QDir::Files | QDir::NoDotAndDotDot); userDir.setNameFilters({"*.json"}); userDir.setSorting(QDir::Name | QDir::Reversed);
    QStringList fileList = userDir.entryList();
    for (const QString &fileName : fileList) {
        QFile file(userDir.filePath(fileName));
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QByteArray data = file.readAll(); file.close();
            QJsonParseError parseError; QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
            if (parseError.error == QJsonParseError::NoError && doc.isArray()) {
                QJsonArray array = doc.array();
                if (!array.isEmpty()) {
                    QJsonObject lastObj = array.last().toObject();
                    if (lastObj.contains("uts")) {
                        qint64 lastTimestampInFile = lastObj["uts"].toInteger();
                        if (lastTimestampInFile > 0) return lastTimestampInFile;
                    }
                }
            }
        }
    }
    return 0;
}
