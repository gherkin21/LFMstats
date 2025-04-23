#ifndef LASTFMMANAGER_H
#define LASTFMMANAGER_H

#include <QObject>
#include <QString>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QDateTime>
#include <QList>
#include <QThread>
#include <QTimer> // Added for retry timer
#include "scrobbledata.h"

class LastFmWorker;

class LastFmManager : public QObject {
    Q_OBJECT

    // Define max retries for 500 errors
    static const int MAX_500_RETRIES = 3;
    // Define retry delay in milliseconds (1 minute)
    static const int RETRY_DELAY_MS = 60 * 1000;

public:
    explicit LastFmManager(QObject *parent = nullptr);
    ~LastFmManager();

    void setup(const QString &apiKey, const QString &username);

    // --- Fetch Methods ---
    void fetchScrobblesSince(qint64 lastSyncTimestamp);
    void startInitialOrResumeFetch(int startPage, int knownTotalPages);

signals:
    // Internal signal to worker thread
    void startFetching(const QString &apiKey, const QString &username, qint64 fromTimestamp, int page);

    // --- Output Signals ---
    void pageReadyForSaving(const QList<ScrobbleData> &pageScrobbles, int pageNumber);
    void totalPagesDetermined(int totalPages);
    void fetchFinished(); // Signals end of entire operation (success or final failure)
    void fetchError(const QString &errorString); // Final error signal

private slots:
    // Handles worker result for one page
    void handlePageResultReady(const QList<ScrobbleData> &pageScrobbles, int totalPages, int currentPage);
    // Handles worker error signal (NOW includes status code)
    void handleFetchErrorWorker(const QString &errorString, int httpStatusCode); // MODIFIED signature
    // Handles worker thread's internal finished signal
    void handleWorkerFinished();
    // Slot called by the retry timer
    void retryLastFetch(); // NEW Slot

private:
    void scheduleNextPageFetch(); // Helper
    void resetRetryState(); // Helper to clear retry count/params

    QThread *m_workerThread = nullptr;
    LastFmWorker *m_worker = nullptr;

    // Fetch context
    QString m_apiKey;
    QString m_username;
    qint64 m_fetchFromTimestamp = 0;
    int m_currentPageFetching = 0;
    int m_expectedTotalPages = 0;
    bool m_isPerformingUpdate = false;

    // --- Retry Logic Members ---
    QTimer *m_retryTimer = nullptr; // Timer for delay
    int m_retryCount = 0;           // Current retry attempt number
    // Store parameters of the request that failed with 500
    qint64 m_lastFailed_fromTimestamp = 0;
    int m_lastFailed_page = 0;
    bool m_isRetryPending = false; // Flag if we are currently waiting to retry
};

// --- Worker Class Definition ---
class LastFmWorker : public QObject {
    Q_OBJECT
public:
    explicit LastFmWorker(QObject *parent = nullptr);
public slots:
    // Takes timestamp and page for fetching
    void doFetch(const QString &apiKey, const QString &username, qint64 fromTimestamp, int page);
signals:
    // Emits results including total pages from metadata
    void resultReady(const QList<ScrobbleData> &scrobbles, int totalPages, int currentPage);
    // MODIFIED signal includes HTTP status code (0 if not applicable)
    void errorOccurred(const QString &errorString, int httpStatusCode); // MODIFIED signature
    // Emitted after processing one network request
    void finished();
private slots:
    void onReplyFinished(QNetworkReply *reply);
private:
    QNetworkAccessManager m_networkManager;
    const QString API_BASE_URL = "http://ws.audioscrobbler.com/2.0/";
    const int FETCH_LIMIT = 200;
    // Store context if needed across async reply
    int m_requestedPage = 0;
    qint64 m_requestedFromTimestamp = 0; // Store context for error reporting
};

#endif // LASTFMMANAGER_H
