#ifndef DATABASEMANAGER_H
#define DATABASEMANAGER_H

#include <QObject>
#include <QString>
#include <QList>
#include <QDateTime>
#include <QFuture>
#include <QFutureWatcher>
#include <QQueue>         // For internal save queue
#include <QMutex>         // To protect the queue
#include <QAtomicInteger> // For thread-safe flag/counter
#include "scrobbledata.h"

// Structure for items in the save queue
struct SaveWorkItem {
    int pageNumber; // Or a generic ID if not page-based
    QString username;
    QList<ScrobbleData> data;
};

class DatabaseManager : public QObject {
    Q_OBJECT
public:
    explicit DatabaseManager(const QString &basePath = "db", QObject *parent = nullptr);
    ~DatabaseManager() override = default; // Simple destructor

    // --- Asynchronous SAVE operation ---
    // Adds data to an internal queue and ensures a save task is running.
    void saveScrobblesAsync(int pageNumber, const QString &username, const QList<ScrobbleData> &scrobbles);

    // --- Asynchronous LOAD operations (using QtConcurrent + QFutureWatcher) ---
    void loadScrobblesAsync(const QString &username, const QDateTime &from, const QDateTime &to);
    void loadAllScrobblesAsync(const QString &username);
    qint64 getLastSyncTimestamp(const QString &username);

    // --- State Check ---
    // Checks if the background save task is currently active. Marked const.
    bool isSaveInProgress() const;

signals:
    // --- Save Signals (Emitted by the save task loop) ---
    void pageSaveCompleted(int pageNumber);
    void pageSaveFailed(int pageNumber, const QString &error);

    // --- Load Signals (From QFutureWatcher) ---
    void loadComplete(const QList<ScrobbleData> &scrobbles);
    void loadError(const QString &error);

    // --- Status Signal ---
    void statusMessage(const QString &message);

private slots:
    // --- Load Slot ---
    void handleLoadFinished();
    // REMOVED: void handleSaveFinished(); // No longer using m_saveWatcher

private:
    // --- Helper Methods ---
    // Checks if save task is running; if not, starts it via QtConcurrent::run.
    void startSaveTaskIfNotRunning();
    // The function executed by QtConcurrent::run, processes the save queue.
    void saveTaskLoop();

    // --- Sync Worker Functions (static helpers) ---
    // Performs actual file IO for saving a chunk
    static bool saveChunkSync(const QString &basePath, const QString &username, const QList<ScrobbleData> &scrobbles, QString &errorMsg);
    // Calculates start of week (Monday UTC)
    static QDateTime getWeekStart(const QDateTime timestamp);
    // Gets the file path for a specific week
    static QString getWeekFilePath(const QString &userPath, const QDateTime timestamp);

    // Load sync functions remain static
    static QList<ScrobbleData> loadScrobblesSync(const QString &basePath, const QString &username, const QDateTime &from, const QDateTime &to, QString &errorMsg);
    static QList<ScrobbleData> loadAllScrobblesSync(const QString &basePath, const QString &username, QString &errorMsg);
    static qint64 findLastTimestampSync(const QString& basePath, const QString& username);

    // --- Member Variables ---
    QString m_basePath; // Absolute path to DB base directory

    // Load operations
    QFutureWatcher<QList<ScrobbleData>> m_loadWatcher; // Watches async load tasks
    QString m_lastLoadError;                           // Stores last load error message

    // Save operations (Queue simulation with QtConcurrent)
    // --->>> MUTABLE keyword added here <<<---
    mutable QMutex m_saveQueueMutex;        // Protects access to the save queue (mutable for const isSaveInProgress)
    QQueue<SaveWorkItem> m_saveQueue;       // Queue for pending save operations
    QAtomicInteger<bool> m_saveTaskRunning; // Atomic flag: true if a save task is active in thread pool
};

#endif // DATABASEMANAGER_H
