/**
 * @file databasemanager.cpp
 * @brief Implementation of the DatabaseManager class.
 */

#include "databasemanager.h"
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QMetaObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QThread>
#include <QtConcurrent>
#include <algorithm>

DatabaseManager::DatabaseManager(const QString &basePath, QObject *parent)
    : QObject(parent), m_saveTaskRunning(false) {

  QString absoluteBasePath =
      QDir::isAbsolutePath(basePath)
          ? basePath
          : QDir::cleanPath(QCoreApplication::applicationDirPath() + "/" +
                            basePath);
  m_basePath = absoluteBasePath;
  qInfo() << "Database base path set to:" << m_basePath;
  QDir dir;
  if (!dir.mkpath(m_basePath)) {
    qCritical() << "Could not create base database directory:" << m_basePath;
    emit statusMessage("Error: Cannot create DB directory: " + m_basePath);
  }

  connect(&m_loadWatcher, &QFutureWatcherBase::finished, this,
          &DatabaseManager::handleLoadFinished);
}

void DatabaseManager::saveScrobblesAsync(int pageNumber,
                                         const QString &username,
                                         const QList<ScrobbleData> &scrobbles) {
  if (username.isEmpty()) {
    emit pageSaveFailed(pageNumber, "Cannot save data for empty username.");
    qWarning() << "[DB Manager] Save requested with empty username for page"
               << pageNumber;
    return;
  }
  if (scrobbles.isEmpty()) {
    qDebug()
        << "[DB Manager] Skipping save request for empty scrobble list (page"
        << pageNumber << ")";
    return;
  }

  SaveWorkItem item;
  item.pageNumber = pageNumber;
  item.username = username;
  item.data = scrobbles;

  qDebug() << "[DB Manager] Adding save request for page" << pageNumber
           << "to queue.";
  {
    QMutexLocker locker(&m_saveQueueMutex);
    m_saveQueue.enqueue(item);
  }

  startSaveTaskIfNotRunning();
}

void DatabaseManager::startSaveTaskIfNotRunning() {
  if (m_saveTaskRunning.testAndSetAcquire(false, true)) {
    qInfo()
        << "[DB Manager] Starting background save task loop (QtConcurrent)...";

    QtConcurrent::run([this]() { this->saveTaskLoop(); });
  } else {
    qDebug() << "[DB Manager] Save task already running.";
  }
}

void DatabaseManager::saveTaskLoop() {
  qInfo() << "[DB Save Task] Started processing queue in thread"
          << QThread::currentThreadId();
  bool processedItemThisLoop = false;

  forever {
    SaveWorkItem item;
    bool itemDequeued = false;

    {
      QMutexLocker locker(&m_saveQueueMutex);
      if (!m_saveQueue.isEmpty()) {
        item = m_saveQueue.dequeue();
        itemDequeued = true;
        qInfo() << "[DB Save Task] Dequeued save request for page"
                << item.pageNumber << ". Items left:" << m_saveQueue.size();
      } else {
        qInfo() << "[DB Save Task] Queue is empty. Finishing task loop.";
        m_saveTaskRunning.storeRelease(false);
        break;
      }

      if (itemDequeued) {
        processedItemThisLoop = true;
        emit statusMessage(QString("Saving page %1...").arg(item.pageNumber));
        QString errorMsg;
        qDebug() << "[DB Save Task] >>> Calling saveChunkSync for page"
                 << item.pageNumber << "...";
        bool success =
            saveChunkSync(m_basePath, item.username, item.data, errorMsg);
        qDebug() << "[DB Save Task] <<< saveChunkSync returned:" << success
                 << "for page" << item.pageNumber << "Error:" << errorMsg;

        if (success) {
          emit pageSaveCompleted(item.pageNumber);
        } else {
          emit pageSaveFailed(item.pageNumber, errorMsg);
        }

        emit statusMessage(QString("Idle. (Last save: Page %1 %2)")
                               .arg(item.pageNumber)
                               .arg(success ? "OK" : "Failed"));
      }
    }

    qInfo() << "[DB Save Task] Exiting save task loop function in thread"
            << QThread::currentThreadId();

    {
      QMutexLocker locker(&m_saveQueueMutex);
      if (!m_saveQueue.isEmpty()) {
        qWarning() << "[DB Save Task] Queue is not empty after loop exit! "
                      "Restarting task...";

        startSaveTaskIfNotRunning();
      } else {
        qDebug() << "[DB Save Task] Confirmed queue empty on loop exit.";
      }
    }
  }
}

bool DatabaseManager::isSaveInProgress() const {
  bool taskRunning = m_saveTaskRunning.loadAcquire();

  int queueSize = 0;
  {
    QMutexLocker locker(&m_saveQueueMutex);
    queueSize = m_saveQueue.size();
  }
  return taskRunning || (queueSize > 0);
}

void DatabaseManager::loadScrobblesAsync(const QString &username,
                                         const QDateTime &from,
                                         const QDateTime &to) {
  if (m_loadWatcher.isRunning()) {
    emit loadError("Load operation (range) already in progress.");
    return;
  }
  if (username.isEmpty()) {
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
  if (username.isEmpty()) {
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
  if (username.isEmpty()) {
    qWarning() << "Cannot get last sync timestamp for empty username.";
    return 0;
  }
  emit statusMessage("Checking last sync time...");
  qint64 timestamp = findLastTimestampSync(m_basePath, username);
  return timestamp;
}

void DatabaseManager::handleLoadFinished() {
  QList<ScrobbleData> results = m_loadWatcher.result();
  emit statusMessage("Idle.");
  if (m_lastLoadError.isEmpty()) {
    emit loadComplete(results);
    qInfo() << "Database load via QtConcurrent finished successfully. Items:"
            << results.count();
  } else {
    emit loadError(m_lastLoadError);
    qWarning() << "Database load via QtConcurrent finished with errors:"
               << m_lastLoadError;
  }
  m_lastLoadError.clear();
}

bool DatabaseManager::saveChunkSync(const QString &basePath,
                                    const QString &username,
                                    const QList<ScrobbleData> &scrobbles,
                                    QString &errorMsg) {
  qDebug() << "[DB Sync Save] Entered saveChunkSync for user" << username
           << "Scrobble Count:" << scrobbles.count();
  errorMsg.clear();

  if (username.isEmpty()) {
    errorMsg = "Username cannot be empty.";
    qWarning() << "[DB Sync Save] " << errorMsg;
    return false;
  }
  if (scrobbles.isEmpty()) {
    qDebug() << "[DB Sync Save] Received empty scrobble list, skipping save.";
    return true;
  }

  QString userPath = basePath + "/" + username;
  QDir dir(userPath);
  qDebug() << "[DB Sync Save] Target user path:" << userPath;

  if (!dir.exists()) {
    qDebug()
        << "[DB Sync Save] User path does not exist, attempting to create.";
    if (!QDir().mkpath(userPath)) {
      errorMsg = "Could not create user directory: " + userPath;
      qCritical() << "[DB Sync Save] " << errorMsg;
      return false;
    }
    qDebug() << "[DB Sync Save] Successfully created user path.";
  }

  QMap<QString, QList<ScrobbleData>> scrobblesByFile;
  for (const ScrobbleData &scrobble : scrobbles) {
    QString filePath = getWeekFilePath(userPath, scrobble.timestamp);
    scrobblesByFile[filePath].append(scrobble);
  }
  qDebug() << "[DB Sync Save] Grouped scrobbles into" << scrobblesByFile.count()
           << "target files.";

  bool all_ok = true;
  QString cumulativeErrors;

  for (auto it = scrobblesByFile.constBegin(); it != scrobblesByFile.constEnd();
       ++it) {
    const QString &filePath = it.key();
    const QList<ScrobbleData> &newScrobblesForFile = it.value();
    QString currentFileError;
    qDebug() << "[DB Sync Save] Processing file:"
             << QFileInfo(filePath).fileName() << "with"
             << newScrobblesForFile.count() << "new entries.";

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
            if (obj.contains("uts") && obj.contains("artist") &&
                obj.contains("track")) {
              ScrobbleData s;
              s.timestamp = QDateTime::fromSecsSinceEpoch(
                  obj["uts"].toInteger(), Qt::UTC);
              if (s.timestamp.isValid() && obj["uts"].toInteger() > 0) {
                s.artist = obj["artist"].toString();
                s.track = obj["track"].toString();
                s.album = obj["album"].toString();
                existingScrobbles.append(s);
                existingTimestamps[s.timestamp.toSecsSinceEpoch()] = true;
              }
            }
          }
          qDebug() << "[DB Sync Save] Read" << existingScrobbles.count()
                   << "valid existing entries from"
                   << QFileInfo(filePath).fileName();
        } else {
          qWarning() << "[DB Sync Save] File exists but is corrupt/not array:"
                     << QFileInfo(filePath).fileName() << ". Overwriting.";
          existingScrobbles.clear();
          existingTimestamps.clear();
        }
      } else {
        currentFileError = "Could not open existing file for reading: " +
                           QFileInfo(filePath).fileName() +
                           " Error: " + readFile.errorString();
        qWarning() << "[DB Sync Save] " << currentFileError;
        all_ok = false;
        cumulativeErrors += currentFileError + "; ";
        continue;
      }
    }

    int addedCount = 0;
    for (const ScrobbleData &newScrobble : newScrobblesForFile) {
      if (newScrobble.timestamp.isValid() &&
          newScrobble.timestamp.toSecsSinceEpoch() > 0) {
        if (!existingTimestamps.contains(
                newScrobble.timestamp.toSecsSinceEpoch())) {
          existingScrobbles.append(newScrobble);
          addedCount++;
        }
      }
    }
    if (addedCount == 0) {
      qDebug() << "[DB Sync Save] No unique entries to add for"
               << QFileInfo(filePath).fileName() << ". Skipping write.";
      continue;
    }
    qDebug() << "[DB Sync Save] Added" << addedCount
             << "unique entries. Total for file now:"
             << existingScrobbles.count();

    std::sort(existingScrobbles.begin(), existingScrobbles.end(),
              [](const ScrobbleData &a, const ScrobbleData &b) {
                return a.timestamp < b.timestamp;
              });

    QJsonArray outputArray;
    for (const ScrobbleData &s : existingScrobbles) {
      QJsonObject obj;
      obj["artist"] = s.artist;
      obj["track"] = s.track;
      obj["album"] = s.album;
      obj["uts"] = s.timestamp.toSecsSinceEpoch();
      outputArray.append(obj);
    }
    QJsonDocument outputDoc(outputArray);
    QSaveFile saveFile(filePath);
    if (saveFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
      saveFile.write(outputDoc.toJson(QJsonDocument::Compact));
      if (!saveFile.commit()) {
        currentFileError = "Failed to commit changes to file: " +
                           QFileInfo(filePath).fileName() +
                           " Error: " + saveFile.errorString();
        qCritical() << "[DB Sync Save] COMMIT FAILED:" << currentFileError;
        all_ok = false;
        cumulativeErrors += currentFileError + "; ";
      } else {
        qDebug() << "[DB Sync Save] Successfully committed"
                 << QFileInfo(filePath).fileName();
      }
    } else {
      currentFileError = "Could not open QSaveFile for writing: " +
                         QFileInfo(filePath).fileName() +
                         " Error: " + saveFile.errorString();
      qWarning() << "[DB Sync Save] QSaveFile open failed:" << currentFileError;
      all_ok = false;
      cumulativeErrors += currentFileError + "; ";
    }
  }

  if (!all_ok) {
    errorMsg = cumulativeErrors;
  }
  return all_ok;
}

QDateTime DatabaseManager::getWeekStart(const QDateTime timestamp) {
  QDateTime utcTimestamp = timestamp.toUTC();
  int daysToSubtract = utcTimestamp.date().dayOfWeek() - 1;
  if (daysToSubtract < 0)
    daysToSubtract = 6;
  return utcTimestamp.date().addDays(-daysToSubtract).startOfDay(Qt::UTC);
}

QString DatabaseManager::getWeekFilePath(const QString &userPath,
                                         const QDateTime timestamp) {
  QDateTime weekStart = getWeekStart(timestamp);
  return QString("%1/%2.json").arg(userPath).arg(weekStart.toSecsSinceEpoch());
}

QList<ScrobbleData> DatabaseManager::loadScrobblesSync(const QString &basePath,
                                                       const QString &username,
                                                       const QDateTime &from,
                                                       const QDateTime &to,
                                                       QString &errorMsg) {
  qDebug() << "[Load Worker] Loading scrobbles for" << username << "from"
           << from.toString(Qt::ISODate) << "to" << to.toString(Qt::ISODate);
  QList<ScrobbleData> loadedScrobbles;
  QString userPath = basePath + "/" + username;
  QDir userDir(userPath);
  if (!userDir.exists()) {
    return loadedScrobbles;
  }
  userDir.setFilter(QDir::Files | QDir::NoDotAndDotDot);
  userDir.setNameFilters({"*.json"});
  userDir.setSorting(QDir::Name);
  QStringList fileList = userDir.entryList();
  for (const QString &fileName : fileList) {
    qint64 fileTimestamp = fileName.chopped(5).toLongLong();
    if (fileTimestamp == 0 && fileName != "0.json")
      continue;
    QDateTime fileWeekStart =
        QDateTime::fromSecsSinceEpoch(fileTimestamp, Qt::UTC);
    QDateTime fileWeekEnd = fileWeekStart.addDays(7);
    if (fileWeekEnd <= from || fileWeekStart >= to)
      continue;
    QFile file(userDir.filePath(fileName));
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
      QByteArray data = file.readAll();
      file.close();
      QJsonParseError parseError;
      QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
      if (parseError.error == QJsonParseError::NoError && doc.isArray()) {
        QJsonArray array = doc.array();
        for (const QJsonValue &val : array) {
          QJsonObject obj = val.toObject();
          if (obj.contains("uts") && obj.contains("artist") &&
              obj.contains("track")) {
            ScrobbleData s;
            qint64 uts = obj["uts"].toInteger();
            s.timestamp = QDateTime::fromSecsSinceEpoch(uts, Qt::UTC);
            if (s.timestamp.isValid() && uts > 0 && s.timestamp >= from &&
                s.timestamp < to) {
              s.artist = obj["artist"].toString();
              s.track = obj["track"].toString();
              s.album = obj["album"].toString();
              loadedScrobbles.append(s);
            }
          }
        }
      } else {
        errorMsg += "Corrupt file: " + fileName + "; ";
      }
    } else {
      errorMsg += "Cannot read file: " + fileName + "; ";
    }
  }
  std::sort(loadedScrobbles.begin(), loadedScrobbles.end(),
            [](const ScrobbleData &a, const ScrobbleData &b) {
              return a.timestamp < b.timestamp;
            });
  return loadedScrobbles;
}
QList<ScrobbleData> DatabaseManager::loadAllScrobblesSync(
    const QString &basePath, const QString &username, QString &errorMsg) {
  QDateTime distantPast = QDateTime::fromSecsSinceEpoch(0, Qt::UTC);
  QDateTime distantFuture = QDateTime::currentDateTimeUtc().addYears(10);
  return loadScrobblesSync(basePath, username, distantPast, distantFuture,
                           errorMsg);
}
qint64 DatabaseManager::findLastTimestampSync(const QString &basePath,
                                              const QString &username) {
  QString userPath = basePath + "/" + username;
  QDir userDir(userPath);
  if (!userDir.exists())
    return 0;
  userDir.setFilter(QDir::Files | QDir::NoDotAndDotDot);
  userDir.setNameFilters({"*.json"});
  userDir.setSorting(QDir::Name | QDir::Reversed);
  QStringList fileList = userDir.entryList();
  for (const QString &fileName : fileList) {
    QFile file(userDir.filePath(fileName));
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
      QByteArray data = file.readAll();
      file.close();
      QJsonParseError parseError;
      QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
      if (parseError.error == QJsonParseError::NoError && doc.isArray()) {
        QJsonArray array = doc.array();
        if (!array.isEmpty()) {
          QJsonObject lastObj = array.last().toObject();
          if (lastObj.contains("uts")) {
            qint64 lastTimestampInFile = lastObj["uts"].toInteger();
            if (lastTimestampInFile > 0)
              return lastTimestampInFile;
          }
        }
      }
    }
  }
  return 0;
}
