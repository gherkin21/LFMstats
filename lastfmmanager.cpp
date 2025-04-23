#include "lastfmmanager.h"
#include <QUrl>
#include <QUrlQuery>
#include <QDebug>
#include <QTimer>
#include <QNetworkRequest>
#include <QNetworkReply>

// --- LastFmManager Implementation ---
LastFmManager::LastFmManager(QObject *parent)
    : QObject(parent),
    m_apiKey(""), // Initialize strings
    m_username(""),
    m_fetchFromTimestamp(0),
    m_currentPageFetching(0),
    m_expectedTotalPages(0),
    m_isPerformingUpdate(false),
    m_retryCount(0),
    m_lastFailed_fromTimestamp(0),
    m_lastFailed_page(0),
    m_isRetryPending(false)
{
    // --- Setup Retry Timer ---
    m_retryTimer = new QTimer(this);
    m_retryTimer->setInterval(RETRY_DELAY_MS); // 1 minute
    m_retryTimer->setSingleShot(true); // Only fire once per start()
    connect(m_retryTimer, &QTimer::timeout, this, &LastFmManager::retryLastFetch);
    // --- End Timer Setup ---

    // --- Setup Worker Thread ---
    m_workerThread = new QThread(this);
    m_workerThread->setObjectName("LastFmWorkerThread");
    m_worker = new LastFmWorker();
    m_worker->moveToThread(m_workerThread);

    // Connect signals (Worker error signal now has status code)
    connect(m_workerThread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(this, &LastFmManager::startFetching, m_worker, &LastFmWorker::doFetch, Qt::QueuedConnection);
    connect(m_worker, &LastFmWorker::resultReady, this, &LastFmManager::handlePageResultReady, Qt::QueuedConnection);
    // Connect worker error to slot accepting status code
    connect(m_worker, &LastFmWorker::errorOccurred, this, &LastFmManager::handleFetchErrorWorker, Qt::QueuedConnection); // <<< MODIFIED Connection
    connect(m_worker, &LastFmWorker::finished, this, &LastFmManager::handleWorkerFinished, Qt::QueuedConnection);

    m_workerThread->start();
    qInfo() << "LastFmManager worker thread started.";
}

LastFmManager::~LastFmManager() {
    qDebug() << "LastFmManager Destructor: Stopping worker thread...";
    if(m_workerThread && m_workerThread->isRunning()) {
        m_workerThread->quit();
        if (!m_workerThread->wait(3000)) {
            qWarning() << "Last.fm worker thread did not stop gracefully, terminating.";
            m_workerThread->terminate(); m_workerThread->wait();
        } else {
            qDebug() << "Last.fm worker thread stopped gracefully.";
        }
    }
    qInfo() << "LastFmManager destroyed.";
}

void LastFmManager::setup(const QString &apiKey, const QString &username) {
    qDebug() << "[LFM Manager] setup called. API Key:" << (apiKey.isEmpty() ? "EMPTY" : "SET") << "Username:" << username;
    m_apiKey = apiKey;
    m_username = username;
}

// Helper to reset retry state before starting a new operation
void LastFmManager::resetRetryState() {
    qDebug() << "[LFM Manager] Resetting retry state.";
    m_retryTimer->stop(); // Stop timer if it was running
    m_retryCount = 0;
    m_lastFailed_fromTimestamp = 0;
    m_lastFailed_page = 0;
    m_isRetryPending = false;
}

// For UPDATES after initial fetch is complete
void LastFmManager::fetchScrobblesSince(qint64 lastSyncTimestamp) {
    // --->>> ADD LOG <<<---
    qDebug() << "[LFM Manager] fetchScrobblesSince: Checking Key/User before emit. Key:" << (m_apiKey.isEmpty() ? "EMPTY" : "SET") << "User:" << m_username;
    // --->>> END LOG <<<---
    if (m_apiKey.isEmpty() || m_username.isEmpty()) { emit fetchError("API Key or Username not set."); return; }

    qInfo() << "Starting UPDATE fetch since timestamp" << lastSyncTimestamp;
    resetRetryState(); // Ensure no pending retries from previous operations
    m_fetchFromTimestamp = lastSyncTimestamp;
    m_currentPageFetching = 1;
    m_expectedTotalPages = 0;
    m_isPerformingUpdate = true;
    emit startFetching(m_apiKey, m_username, m_fetchFromTimestamp, m_currentPageFetching);
}

// For INITIAL fetch or RESUMING a failed initial fetch
void LastFmManager::startInitialOrResumeFetch(int startPage, int knownTotalPages) {
    // --->>> ADD LOG <<<---
    qDebug() << "[LFM Manager] startInitialOrResumeFetch: Checking Key/User before emit. Key:" << (m_apiKey.isEmpty() ? "EMPTY" : "SET") << "User:" << m_username;
    // --->>> END LOG <<<---
    if (m_apiKey.isEmpty() || m_username.isEmpty()) { emit fetchError("API Key or Username not set."); return; }

    qInfo() << "Starting INITIAL/RESUME fetch from page" << startPage << "(Known total:" << knownTotalPages << ")";
    resetRetryState(); // Ensure no pending retries
    m_fetchFromTimestamp = 0;
    m_currentPageFetching = qMax(1, startPage);
    m_expectedTotalPages = knownTotalPages;
    m_isPerformingUpdate = false;
    if(m_expectedTotalPages > 0) { emit totalPagesDetermined(m_expectedTotalPages); }
    emit startFetching(m_apiKey, m_username, m_fetchFromTimestamp, m_currentPageFetching);
}

// Schedules the fetch for the next page IF NOT waiting for a retry
void LastFmManager::scheduleNextPageFetch() {
    // Do not schedule next page if we are waiting for a retry timer
    if (m_isRetryPending) {
        qDebug() << "[LFM Manager] Skipping next page schedule, retry pending.";
        return;
    }

    // Check if there are more pages expected
    if (m_expectedTotalPages > 0 && m_currentPageFetching < m_expectedTotalPages) {
        m_currentPageFetching++;
        qDebug() << "[LFM Manager] Scheduling fetch for page" << m_currentPageFetching << "in 500ms...";
        QTimer::singleShot(500, this, [=]() {
            // Double-check if retry became pending while timer was waiting
            if (!m_isRetryPending) {
                qDebug() << "[LFM Manager] Timer fired, emitting startFetching for page" << m_currentPageFetching;
                // --->>> ADD LOG <<<---
                qDebug() << "[LFM Manager] scheduleNextPageFetch (timer): Checking Key/User before emit. Key:" << (m_apiKey.isEmpty() ? "EMPTY" : "SET") << "User:" << m_username;
                // --->>> END LOG <<<---
                emit startFetching(m_apiKey, m_username, m_fetchFromTimestamp, m_currentPageFetching);
            } else {
                qDebug() << "[LFM Manager] Timer fired, but retry is now pending. Aborting scheduled fetch.";
            }
        });
    } else {
        // All expected pages fetched or total unknown/reached
        qInfo() << "[LFM Manager] Finished fetching all expected pages from API (last req page" << m_currentPageFetching << " of " << m_expectedTotalPages << ").";
        emit fetchFinished(); // Signal completion of the API fetch part
    }
}

// Handles result for ONE page from the worker
void LastFmManager::handlePageResultReady(const QList<ScrobbleData> &pageScrobbles, int totalPages, int currentPage) {
    qInfo() << "[LFM Manager] Fetched page" << currentPage << "/" << totalPages << "with" << pageScrobbles.count() << "scrobbles.";

    // If this was a successful retry, reset the counter
    if (m_retryCount > 0) {
        qInfo() << "[LFM Manager] Successful fetch after" << m_retryCount << "retry attempt(s). Resetting retry state.";
        resetRetryState(); // Clears count and pending flag
    } else {
        // Also clear if it wasn't a retry but we are no longer pending (should already be false)
        m_isRetryPending = false;
    }

    // Update expected total pages logic (same as before)
    if ((m_expectedTotalPages <= 0 && totalPages > 0) || currentPage == 1 || (!m_isPerformingUpdate && totalPages != m_expectedTotalPages) ) {
        if (!m_isPerformingUpdate && m_expectedTotalPages > 0 && m_expectedTotalPages != totalPages) { qWarning() << "[LFM Manager] API reported totalPages changed during fetch! Old:" << m_expectedTotalPages << "New:" << totalPages; }
        if (m_expectedTotalPages != totalPages) { m_expectedTotalPages = totalPages; emit totalPagesDetermined(m_expectedTotalPages); qInfo() << "[LFM Manager] Total pages determined/updated:" << m_expectedTotalPages; }
    }

    // Handle update completion (same as before)
    if (m_isPerformingUpdate && pageScrobbles.isEmpty() && m_fetchFromTimestamp > 0 && currentPage == 1) {
        qInfo() << "[LFM Manager] Update fetch received empty first page, assuming caught up.";
        emit fetchFinished(); return;
    }

    // Emit data for saving
    qDebug() << "[LFM Manager] Emitting pageReadyForSaving for page" << currentPage;
    emit pageReadyForSaving(pageScrobbles, currentPage);

    // Schedule the next page fetch (checks retry pending flag internally)
    scheduleNextPageFetch();
}

// Handles errors reported by the worker (NOW includes status code)
void LastFmManager::handleFetchErrorWorker(const QString &errorString, int httpStatusCode) {
    qWarning() << "[LFM Manager] Fetch error received from Worker:" << errorString << "| HTTP Status:" << httpStatusCode;

    // Check if it's a 500 error and if we haven't exceeded retry limit
    if (httpStatusCode == 500 && m_retryCount < MAX_500_RETRIES) {
        m_retryCount++;
        // Store the parameters of the failed request
        m_lastFailed_fromTimestamp = m_fetchFromTimestamp; // Store context of the *overall* operation
        m_lastFailed_page = m_currentPageFetching; // Store the *specific page* that failed
        m_isRetryPending = true; // Set flag indicating we are waiting

        qWarning() << "[LFM Manager] Received HTTP 500 error for page" << m_lastFailed_page
                   << ". Attempting retry" << m_retryCount << "/" << MAX_500_RETRIES
                   << "in" << (RETRY_DELAY_MS / 1000) << "seconds...";

        m_retryTimer->start(); // Start the 1-minute timer
        // DO NOT emit fetchError or fetchFinished yet
    } else {
        // It's not a 500 error, or retries are exhausted
        QString finalErrorString = errorString; // Default to original error
        if (httpStatusCode == 500) { // Retries exhausted
            qCritical() << "[LFM Manager] HTTP 500 error persisted after" << m_retryCount << "retries for page" << m_currentPageFetching << ". Giving up.";
            finalErrorString = "API Internal Server Error (500) persisted after retries."; // Provide clearer final error
        } else { // Other type of error
            qWarning() << "[LFM Manager] Non-500 error or non-HTTP error occurred. No retry.";
            // finalErrorString remains the original error
        }
        resetRetryState(); // Clear any previous retry state
        emit fetchError(finalErrorString); // Forward the final error
        emit fetchFinished(); // Signal fetch finished (due to final error)
    }
}

// Slot called when the retry timer expires
void LastFmManager::retryLastFetch() {
    qInfo() << "[LFM Manager] Retry timer expired. Retrying fetch for page" << m_lastFailed_page;
    m_isRetryPending = false; // No longer waiting for the timer

    // It's possible m_retryCount was reset by a successful *different* page while waiting.
    // This is okay, proceed with retry using the stored failed page info.

    // --->>> ADD LOG <<<---
    qDebug() << "[LFM Manager] retryLastFetch: Checking Key/User before emit. Key:" << (m_apiKey.isEmpty() ? "EMPTY" : "SET") << "User:" << m_username;
    // --->>> END LOG <<<---

    // Use the stored parameters to re-emit the startFetching signal for the FAILED page
    emit startFetching(m_apiKey, m_username, m_lastFailed_fromTimestamp, m_lastFailed_page);
    // The retry count is handled when the result/error comes back for this attempt
}


// Handles the worker's internal signal after each task
void LastFmManager::handleWorkerFinished() {
    // qDebug() << "[LFM Manager] Worker task finished processing in its thread.";
}


// --- LastFmWorker Implementation ---
LastFmWorker::LastFmWorker(QObject *parent) : QObject(parent), m_requestedPage(0), m_requestedFromTimestamp(0) {}

// Slot called to fetch a specific page
void LastFmWorker::doFetch(const QString &apiKey, const QString &username, qint64 fromTimestamp, int page) {
    // --->>> ADD LOG <<<---
    qCritical() << "[Worker Thread] doFetch received: API Key is" << (apiKey.isEmpty() ? "EMPTY" : "SET") << "Username:" << username << "Page:" << page;
    // --->>> END LOG <<<---

    m_requestedPage = page; // Store context
    m_requestedFromTimestamp = fromTimestamp; // Store context

    // --->>> ADD CHECK <<<---
    if (apiKey.isEmpty() || username.isEmpty()) {
        qCritical() << "[Worker Thread] ABORTING fetch: API Key or Username is empty on arrival!";
        // Emit an error signal immediately from the worker
        emit errorOccurred("Internal Error: API Key/User empty in worker", 0); // Status code 0 for internal error
        emit finished(); // Signal task end
        return;
    }
    // --->>> END CHECK <<<---

    QUrl url(API_BASE_URL);
    QUrlQuery query;
    // ... (Build query parameters as before: method, user, key, format, page, limit, from) ...
    query.addQueryItem("method", "user.getrecenttracks"); query.addQueryItem("user", username); query.addQueryItem("api_key", apiKey); query.addQueryItem("format", "json");
    query.addQueryItem("page", QString::number(page)); query.addQueryItem("limit", QString::number(FETCH_LIMIT));
    if (fromTimestamp > 0) { query.addQueryItem("from", QString::number(fromTimestamp + 1)); }
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

    if (m_networkManager.thread() != QThread::currentThread()) { qWarning() << "[Worker Thread] NAM needs creation/recreation."; }

    qInfo() << "[Worker Thread] Requesting URL:" << request.url().toString();
    QNetworkReply *reply = m_networkManager.get(request);

    connect(reply, &QNetworkReply::finished, this, [=]() { onReplyFinished(reply); });
    connect(reply, &QNetworkReply::errorOccurred, this, [=](QNetworkReply::NetworkError code){
        qWarning() << "[Worker Thread] Network Error Signal (" << reply->url().path() << "):" << code << reply->errorString();
    });
}

// Handles reply for a page fetch
void LastFmWorker::onReplyFinished(QNetworkReply *reply) {
    if (!reply) { qWarning() << "[Worker] Null reply"; emit errorOccurred("Network reply null", 0); emit finished(); return; }

    QUrl url = reply->url();
    int httpStatusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    QByteArray responseData = reply->readAll();

    qInfo() << "[Worker Thread] Reply finished for page" << m_requestedPage << url.query()
            << "| Status:" << httpStatusCode << "| Error:" << reply->errorString();

    QList<ScrobbleData> fetchedScrobbles;
    int totalPages = 0;
    int currentPage = m_requestedPage;

    if (reply->error() != QNetworkReply::NoError || httpStatusCode >= 400) {
        qWarning() << "[Worker Thread] ------ ERROR RESPONSE Page" << m_requestedPage << "------";
        qWarning() << "[Worker Thread] Response Body:" << responseData;
        qWarning() << "[Worker Thread] -----------------------------";
        // Emit error WITH status code
        emit errorOccurred(QString("Network/API Error (Status %1): %2").arg(httpStatusCode).arg(reply->errorString()), httpStatusCode);
    } else {
        QJsonParseError parseError;
        QJsonDocument jsonDoc = QJsonDocument::fromJson(responseData, &parseError);
        if (parseError.error == QJsonParseError::NoError && jsonDoc.isObject()) {
            QJsonObject rootObj = jsonDoc.object();
            if (rootObj.contains("error")) {
                qWarning() << "[Worker Thread] API Error in JSON (Page" << m_requestedPage << "):" << rootObj["message"].toString();
                // Emit error (status code 0 as it's API logic error)
                emit errorOccurred("Last.fm API Error: " + rootObj["message"].toString(), 0);
            } else if (rootObj.contains("recenttracks")) {
                QJsonObject recentTracksObj = rootObj["recenttracks"].toObject();
                QJsonObject attr = recentTracksObj["@attr"].toObject();
                totalPages = attr["totalPages"].toString().toInt();
                currentPage = attr["page"].toString().toInt();
                if (currentPage != m_requestedPage && m_requestedPage > 0) { qWarning() << "[Worker] Page mismatch Req:" << m_requestedPage << "Rcv:" << currentPage; }

                QJsonArray tracksArray = recentTracksObj["track"].toArray();
                int tracksParsedOnPage = 0;
                for (const QJsonValue &value : tracksArray) {
                    QJsonObject trackObj = value.toObject();
                    if (trackObj.contains("@attr") && trackObj["@attr"].toObject()["nowplaying"].toString() == "true") continue;
                    if (trackObj.contains("date")) {
                        qint64 uts = trackObj["date"].toObject()["uts"].toString().toLongLong();
                        if (uts > 0) {
                            ScrobbleData scrobble;
                            scrobble.artist = trackObj["artist"].toObject()["#text"].toString();
                            scrobble.track = trackObj["name"].toString();
                            scrobble.album = trackObj["album"].toObject()["#text"].toString();
                            scrobble.timestamp = QDateTime::fromSecsSinceEpoch(uts, Qt::UTC);
                            fetchedScrobbles.append(scrobble);
                            tracksParsedOnPage++;
                        } else { qWarning() << "[Worker] Invalid UTS <= 0"; }
                    } else { qWarning() << "[Worker] Track missing date object"; }
                }
                qInfo() << "[Worker Thread] Successful Response: Page" << currentPage << "/" << totalPages << "| Parsed:" << tracksParsedOnPage;
                emit resultReady(fetchedScrobbles, totalPages, currentPage); // Use actual currentPage from response
            } else { emit errorOccurred("Invalid JSON structure (page " + QString::number(m_requestedPage) + ")", httpStatusCode); }
        } else { emit errorOccurred("Failed to parse JSON (page " + QString::number(m_requestedPage) + ")", httpStatusCode); }
    }
    reply->deleteLater();
    emit finished();
}
