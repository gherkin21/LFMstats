#ifndef DATABASEMANAGER_H
#define DATABASEMANAGER_H

#include "scrobbledata.h"
#include <QAtomicInteger>
#include <QDateTime>
#include <QFuture>
#include <QFutureWatcher>
#include <QList>
#include <QMutex>
#include <QObject>
#include <QQueue>
#include <QString>

/**
 * @struct SaveWorkItem
 * @brief Holds data for a single page save operation queued for processing.
 */
struct SaveWorkItem {
  int pageNumber;   /**< @brief The page number this data belongs to. */
  QString username; /**< @brief The username associated with the data. */
  QList<ScrobbleData> data; /**< @brief The list of scrobbles to be saved. */
};

class TestDatabaseManager;

/**
 * @class DatabaseManager
 * @brief Manages the persistence of scrobble data to the local disk.
 * @details Provides asynchronous methods for saving fetched scrobble pages and
 * loading stored scrobbles. Data is organized into weekly JSON files per user.
 * Uses QtConcurrent for background tasks.
 * @inherits QObject
 */
class DatabaseManager : public QObject {
  Q_OBJECT

  friend class TestDatabaseManager;

public:
  /**
   * @brief Constructs a DatabaseManager instance.
   * @param basePath The relative or absolute path to the root directory for the
   * database files. If relative, it's resolved against the application
   * directory. Defaults to "db".
   * @param parent The parent QObject, defaults to nullptr.
   */
  explicit DatabaseManager(const QString &basePath = "db",
                           QObject *parent = nullptr);

  /**
   * @brief Default destructor.
   */
  ~DatabaseManager() override = default;

  /**
   * @brief Asynchronously queues a list of scrobbles for saving to the
   * database.
   * @details The save operation is performed in a background thread. Empty
   * usernames or scrobble lists are handled gracefully.
   * @param pageNumber The page number corresponding to this batch of scrobbles
   * (used for signalling).
   * @param username The Last.fm username the scrobbles belong to. Cannot be
   * empty.
   * @param scrobbles The list of ScrobbleData items to save.
   */
  void saveScrobblesAsync(int pageNumber, const QString &username,
                          const QList<ScrobbleData> &scrobbles);

  /**
   * @brief Asynchronously loads scrobbles for a specific user within a given
   * UTC date range using QtConcurrent.
   * @details Connect to loadComplete or loadError signals for results. Only one
   * load operation (range or all) should be active at a time via this
   * mechanism.
   * @param username The Last.fm username to load data for. Cannot be empty.
   * @param from The start of the UTC date range (inclusive).
   * @param to The end of the UTC date range (exclusive).
   */
  void loadScrobblesAsync(const QString &username, const QDateTime &from,
                          const QDateTime &to);

  /**
   * @brief Asynchronously loads all stored scrobbles for a specific user using
   * QtConcurrent.
   * @details Connect to loadComplete or loadError signals for results. Only one
   * load operation (range or all) should be active at a time via this
   * mechanism.
   * @param username The Last.fm username to load data for. Cannot be empty.
   */
  void loadAllScrobblesAsync(const QString &username);

  /**
   * @brief Synchronously retrieves the timestamp of the latest scrobble stored
   * in the database for a given user.
   * @param username The Last.fm username to check. Cannot be empty.
   * @return The UTC timestamp (seconds since epoch) of the last known scrobble,
   * or 0 if no data exists or the user is empty.
   */
  qint64 getLastSyncTimestamp(const QString &username);

  /**
   * @brief Checks if there are pending save operations either running or
   * queued.
   * @return True if the background save task is active or the save queue is not
   * empty, false otherwise.
   */
  bool isSaveInProgress() const;

  /**
   * @brief Provides access to the base path used by the manager.
   * @return The absolute path string of the database root directory.
   */
  QString getBasePathInternal() const { return m_basePath; }

signals:
  /**
   * @brief Emitted when a specific page of scrobbles has been successfully
   * saved to disk.
   * @param pageNumber The page number that was completed.
   */
  void pageSaveCompleted(int pageNumber);
  /**
   * @brief Emitted when saving a specific page of scrobbles failed.
   * @param pageNumber The page number that failed.
   * @param error A string describing the error.
   */
  void pageSaveFailed(int pageNumber, const QString &error);

  /**
   * @brief Emitted when an asynchronous load operation completes successfully.
   * @param scrobbles A list containing the loaded ScrobbleData, sorted by
   * timestamp.
   */
  void loadComplete(const QList<ScrobbleData> &scrobbles);

  /**
   * @brief Emitted when an asynchronous load operation fails.
   * @param error A string describing the load error.
   */
  void loadError(const QString &error);

  /**
   * @brief Emitted to provide status updates about database operations (saving,
   * loading, errors).
   * @param message The status message string.
   */
  void statusMessage(const QString &message);

private slots:
  /**
   * @brief Slot connected to the load watcher's finished signal to handle
   * results or errors from async load operations.
   */
  void handleLoadFinished();

private:
  /**
   * @brief Starts the background save task via QtConcurrent if it's not already
   * running.
   */
  void startSaveTaskIfNotRunning();

  /**
   * @brief The main loop executed by the background save task thread.
   * @details Dequeues SaveWorkItem instances and calls saveChunkSync until the
   * queue is empty.
   */
  void saveTaskLoop();

  /**
   * @brief Synchronously saves or merges a list of scrobbles into the
   * appropriate weekly JSON file(s).
   * @details Reads existing data, merges new unique scrobbles, sorts, and
   * writes back using QSaveFile.
   * @param basePath The root database directory path.
   * @param username The username subdirectory.
   * @param scrobbles The list of new scrobbles to process.
   * @param[out] errorMsg A string to store any error messages encountered.
   * @return True if all operations were successful, false otherwise.
   */
  static bool saveChunkSync(const QString &basePath, const QString &username,
                            const QList<ScrobbleData> &scrobbles,
                            QString &errorMsg);

  /**
   * @brief Calculates the start of the week (Monday, 00:00:00 UTC) containing
   * the given timestamp.
   * @param timestamp The timestamp to determine the week start for.
   * @return A QDateTime representing the start of the week in UTC.
   */
  static QDateTime getWeekStart(const QDateTime timestamp);
  /**
   * @brief Constructs the full file path for the weekly data file corresponding
   * to the given timestamp.
   * @details The filename is based on the Unix timestamp of the week start.
   * @param userPath The path to the specific user's data directory.
   * @param timestamp The timestamp within the desired week.
   * @return A QString containing the full path to the JSON file (e.g.,
   * ".../username/1677456000.json").
   */
  static QString getWeekFilePath(const QString &userPath,
                                 const QDateTime timestamp);

  /**
   * @brief Synchronously loads scrobbles from weekly files within a specified
   * UTC date range.
   * @param basePath The root database directory path.
   * @param username The username subdirectory.
   * @param from The start UTC timestamp (inclusive).
   * @param to The end UTC timestamp (exclusive).
   * @param[out] errorMsg A string to store any error messages encountered
   * (e.g., corrupt files).
   * @return A list of ScrobbleData items within the range, sorted by timestamp.
   */
  static QList<ScrobbleData> loadScrobblesSync(const QString &basePath,
                                               const QString &username,
                                               const QDateTime &from,
                                               const QDateTime &to,
                                               QString &errorMsg);

  /**
   * @brief Synchronously loads all scrobbles for a user by calling
   * loadScrobblesSync with a very wide date range.
   * @param basePath The root database directory path.
   * @param username The username subdirectory.
   * @param[out] errorMsg A string to store any error messages encountered.
   * @return A list of all ScrobbleData items for the user, sorted by timestamp.
   */
  static QList<ScrobbleData> loadAllScrobblesSync(const QString &basePath,
                                                  const QString &username,
                                                  QString &errorMsg);

  /**
   * @brief Synchronously finds the timestamp of the very last scrobble stored
   * across all weekly files for a user.
   * @details Iterates through files in reverse order to find the latest entry
   * efficiently.
   * @param basePath The root database directory path.
   * @param username The username subdirectory.
   * @return The UTC timestamp (seconds since epoch) of the latest scrobble, or
   * 0 if none found.
   */
  static qint64 findLastTimestampSync(const QString &basePath,
                                      const QString &username);

  QString m_basePath;

  QFutureWatcher<QList<ScrobbleData>> m_loadWatcher;
  QString m_lastLoadError;

  mutable QMutex m_saveQueueMutex;
  QQueue<SaveWorkItem> m_saveQueue;
  QAtomicInteger<bool> m_saveTaskRunning;
};

#endif // DATABASEMANAGER_H
