#include "lastfmmanager.h"
#include <QDebug>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

LastFmManager::LastFmManager(QObject *parent)
    : QObject(parent), m_apiKey(""), m_username(""), m_fetchFromTimestamp(0),
      m_currentPageFetching(0), m_expectedTotalPages(0),
      m_isPerformingUpdate(false), m_retryCount(0),
      m_lastFailed_fromTimestamp(0), m_lastFailed_page(0),
      m_isRetryPending(false) {
  m_retryTimer = new QTimer(this);
  m_retryTimer->setInterval(RETRY_DELAY_MS);
  m_retryTimer->setSingleShot(true);
  connect(m_retryTimer, &QTimer::timeout, this, &LastFmManager::retryLastFetch);

  m_workerThread = new QThread(this);
  m_workerThread->setObjectName("LastFmWorkerThread");
  m_worker = new LastFmWorker();
  m_worker->moveToThread(m_workerThread);

  connect(m_workerThread, &QThread::finished, m_worker, &QObject::deleteLater);
  connect(this, &LastFmManager::startFetching, m_worker, &LastFmWorker::doFetch,
          Qt::QueuedConnection);
  connect(m_worker, &LastFmWorker::resultReady, this,
          &LastFmManager::handlePageResultReady, Qt::QueuedConnection);

  connect(m_worker, &LastFmWorker::errorOccurred, this,
          &LastFmManager::handleFetchErrorWorker, Qt::QueuedConnection);
  connect(m_worker, &LastFmWorker::finished, this,
          &LastFmManager::handleWorkerFinished, Qt::QueuedConnection);

  m_workerThread->start();
  qInfo() << "LastFmManager worker thread started.";
}

LastFmManager::~LastFmManager() {
  qDebug() << "LastFmManager Destructor: Stopping worker thread...";
  if (m_workerThread && m_workerThread->isRunning()) {
    m_workerThread->quit();
    if (!m_workerThread->wait(3000)) {
      qWarning()
          << "Last.fm worker thread did not stop gracefully, terminating.";
      m_workerThread->terminate();
      m_workerThread->wait();
    } else {
      qDebug() << "Last.fm worker thread stopped gracefully.";
    }
  }
  qInfo() << "LastFmManager destroyed.";
}

void LastFmManager::setup(const QString &apiKey, const QString &username) {
  qDebug() << "[LFM Manager] setup called. API Key:"
           << (apiKey.isEmpty() ? "EMPTY" : "SET") << "Username:" << username;
  m_apiKey = apiKey;
  m_username = username;
}

void LastFmManager::resetRetryState() {
  qDebug() << "[LFM Manager] Resetting retry state.";
  m_retryTimer->stop();
  m_retryCount = 0;
  m_lastFailed_fromTimestamp = 0;
  m_lastFailed_page = 0;
  m_isRetryPending = false;
}

void LastFmManager::fetchScrobblesSince(qint64 lastSyncTimestamp) {
  qDebug() << "[LFM Manager] fetchScrobblesSince: Checking Key/User before "
              "emit. Key:"
           << (m_apiKey.isEmpty() ? "EMPTY" : "SET") << "User:" << m_username;
  if (m_apiKey.isEmpty() || m_username.isEmpty()) {
    emit fetchError("API Key or Username not set.");
    return;
  }

  qInfo() << "Starting UPDATE fetch since timestamp" << lastSyncTimestamp;
  resetRetryState();
  m_fetchFromTimestamp = lastSyncTimestamp;
  m_currentPageFetching = 1;
  m_expectedTotalPages = 0;
  m_isPerformingUpdate = true;
  emit startFetching(m_apiKey, m_username, m_fetchFromTimestamp,
                     m_currentPageFetching);
}

void LastFmManager::startInitialOrResumeFetch(int startPage,
                                              int knownTotalPages) {
  qDebug() << "[LFM Manager] startInitialOrResumeFetch: Checking Key/User "
              "before emit. Key:"
           << (m_apiKey.isEmpty() ? "EMPTY" : "SET") << "User:" << m_username;
  if (m_apiKey.isEmpty() || m_username.isEmpty()) {
    emit fetchError("API Key or Username not set.");
    return;
  }

  qInfo() << "Starting INITIAL/RESUME fetch from page" << startPage
          << "(Known total:" << knownTotalPages << ")";
  resetRetryState();
  m_fetchFromTimestamp = 0;
  m_currentPageFetching = qMax(1, startPage);
  m_expectedTotalPages = knownTotalPages;
  m_isPerformingUpdate = false;
  if (m_expectedTotalPages > 0) {
    emit totalPagesDetermined(m_expectedTotalPages);
  }
  emit startFetching(m_apiKey, m_username, m_fetchFromTimestamp,
                     m_currentPageFetching);
}

void LastFmManager::scheduleNextPageFetch() {
  if (m_isRetryPending) {
    qDebug() << "[LFM Manager] Skipping next page schedule, retry pending.";
    return;
  }

  if (m_expectedTotalPages > 0 &&
      m_currentPageFetching < m_expectedTotalPages) {
    m_currentPageFetching++;
    qDebug() << "[LFM Manager] Scheduling fetch for page"
             << m_currentPageFetching << "in 500ms...";
    QTimer::singleShot(500, this, [=]() {
      if (!m_isRetryPending) {
        qDebug() << "[LFM Manager] Timer fired, emitting startFetching for page"
                 << m_currentPageFetching;
        qDebug() << "[LFM Manager] scheduleNextPageFetch (timer): Checking "
                    "Key/User before emit. Key:"
                 << (m_apiKey.isEmpty() ? "EMPTY" : "SET")
                 << "User:" << m_username;
        emit startFetching(m_apiKey, m_username, m_fetchFromTimestamp,
                           m_currentPageFetching);
      } else {
        qDebug() << "[LFM Manager] Timer fired, but retry is now pending. "
                    "Aborting scheduled fetch.";
      }
    });
  } else {
    qInfo() << "[LFM Manager] Finished fetching all expected pages from API "
               "(last req page"
            << m_currentPageFetching << " of " << m_expectedTotalPages << ").";
    emit fetchFinished();
  }
}

void LastFmManager::handlePageResultReady(
    const QList<ScrobbleData> &pageScrobbles, int totalPages, int currentPage) {
  qInfo() << "[LFM Manager] Fetched page" << currentPage << "/" << totalPages
          << "with" << pageScrobbles.count() << "scrobbles.";

  if (m_retryCount > 0) {
    qInfo() << "[LFM Manager] Successful fetch after" << m_retryCount
            << "retry attempt(s). Resetting retry state.";
    resetRetryState();
  } else {
    m_isRetryPending = false;
  }

  if ((m_expectedTotalPages <= 0 && totalPages > 0) || currentPage == 1 ||
      (!m_isPerformingUpdate && totalPages != m_expectedTotalPages)) {
    if (!m_isPerformingUpdate && m_expectedTotalPages > 0 &&
        m_expectedTotalPages != totalPages) {
      qWarning()
          << "[LFM Manager] API reported totalPages changed during fetch! Old:"
          << m_expectedTotalPages << "New:" << totalPages;
    }
    if (m_expectedTotalPages != totalPages) {
      m_expectedTotalPages = totalPages;
      emit totalPagesDetermined(m_expectedTotalPages);
      qInfo() << "[LFM Manager] Total pages determined/updated:"
              << m_expectedTotalPages;
    }
  }

  if (m_isPerformingUpdate && pageScrobbles.isEmpty() &&
      m_fetchFromTimestamp > 0 && currentPage == 1) {
    qInfo() << "[LFM Manager] Update fetch received empty first page, assuming "
               "caught up.";
    emit fetchFinished();
    return;
  }

  qDebug() << "[LFM Manager] Emitting pageReadyForSaving for page"
           << currentPage;
  emit pageReadyForSaving(pageScrobbles, currentPage);

  scheduleNextPageFetch();
}

void LastFmManager::handleFetchErrorWorker(const QString &errorString,
                                           int httpStatusCode) {
  qWarning() << "[LFM Manager] Fetch error received from Worker:" << errorString
             << "| HTTP Status:" << httpStatusCode;

  if (httpStatusCode == 500 && m_retryCount < MAX_500_RETRIES) {
    m_retryCount++;
    m_lastFailed_fromTimestamp = m_fetchFromTimestamp;
    m_lastFailed_page = m_currentPageFetching;
    m_isRetryPending = true;

    qWarning() << "[LFM Manager] Received HTTP 500 error for page"
               << m_lastFailed_page << ". Attempting retry" << m_retryCount
               << "/" << MAX_500_RETRIES << "in" << (RETRY_DELAY_MS / 1000)
               << "seconds...";

    m_retryTimer->start();
  } else {
    QString finalErrorString = errorString;
    if (httpStatusCode == 500) {
      qCritical() << "[LFM Manager] HTTP 500 error persisted after"
                  << m_retryCount << "retries for page" << m_currentPageFetching
                  << ". Giving up.";
      finalErrorString =
          "API Internal Server Error (500) persisted after retries.";
    } else {
      qWarning() << "[LFM Manager] Non-500 error or non-HTTP error occurred. "
                    "No retry.";
    }
    resetRetryState();
    emit fetchError(finalErrorString);
    emit fetchFinished();
  }
}

void LastFmManager::retryLastFetch() {
  qInfo() << "[LFM Manager] Retry timer expired. Retrying fetch for page"
          << m_lastFailed_page;
  m_isRetryPending = false;

  qDebug()
      << "[LFM Manager] retryLastFetch: Checking Key/User before emit. Key:"
      << (m_apiKey.isEmpty() ? "EMPTY" : "SET") << "User:" << m_username;

  emit startFetching(m_apiKey, m_username, m_lastFailed_fromTimestamp,
                     m_lastFailed_page);
}

void LastFmManager::handleWorkerFinished() {
  qDebug() << "[LFM Manager] Worker task finished processing in its thread.";
}

LastFmWorker::LastFmWorker(QObject *parent)
    : QObject(parent), m_requestedPage(0), m_requestedFromTimestamp(0) {}

void LastFmWorker::doFetch(const QString &apiKey, const QString &username,
                           qint64 fromTimestamp, int page) {
  qCritical() << "[Worker Thread] doFetch received: API Key is"
              << (apiKey.isEmpty() ? "EMPTY" : "SET") << "Username:" << username
              << "Page:" << page;

  m_requestedPage = page;
  m_requestedFromTimestamp = fromTimestamp;

  if (apiKey.isEmpty() || username.isEmpty()) {
    qCritical() << "[Worker Thread] ABORTING fetch: API Key or Username is "
                   "empty on arrival!";
    emit errorOccurred("Internal Error: API Key/User empty in worker", 0);
    emit finished();
    return;
  }

  QUrl url(API_BASE_URL);
  QUrlQuery query;

  query.addQueryItem("method", "user.getrecenttracks");
  query.addQueryItem("user", username);
  query.addQueryItem("api_key", apiKey);
  query.addQueryItem("format", "json");
  query.addQueryItem("page", QString::number(page));
  query.addQueryItem("limit", QString::number(FETCH_LIMIT));
  if (fromTimestamp > 0) {
    query.addQueryItem("from", QString::number(fromTimestamp + 1));
  }
  url.setQuery(query);

  QNetworkRequest request(url);
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                       QNetworkRequest::NoLessSafeRedirectPolicy);

  if (m_networkManager.thread() != QThread::currentThread()) {
    qWarning() << "[Worker Thread] NAM needs creation/recreation.";
  }

  qInfo() << "[Worker Thread] Requesting URL:" << request.url().toString();
  QNetworkReply *reply = m_networkManager.get(request);

  connect(reply, &QNetworkReply::finished, this,
          [=]() { onReplyFinished(reply); });
  connect(reply, &QNetworkReply::errorOccurred, this,
          [=](QNetworkReply::NetworkError code) {
            qWarning() << "[Worker Thread] Network Error Signal ("
                       << reply->url().path() << "):" << code
                       << reply->errorString();
          });
}

void LastFmWorker::onReplyFinished(QNetworkReply *reply) {
  if (!reply) {
    qWarning() << "[Worker] Null reply";
    emit errorOccurred("Network reply null", 0);
    emit finished();
    return;
  }

  QUrl url = reply->url();
  int httpStatusCode =
      reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
  QByteArray responseData = reply->readAll();

  qInfo() << "[Worker Thread] Reply finished for page" << m_requestedPage
          << url.query() << "| Status:" << httpStatusCode
          << "| Error:" << reply->errorString();

  QList<ScrobbleData> fetchedScrobbles;
  int totalPages = 0;
  int currentPage = m_requestedPage;

  if (reply->error() != QNetworkReply::NoError || httpStatusCode >= 400) {
    qWarning() << "[Worker Thread] ------ ERROR RESPONSE Page"
               << m_requestedPage << "------";
    qWarning() << "[Worker Thread] Response Body:" << responseData;
    qWarning() << "[Worker Thread] -----------------------------";

    emit errorOccurred(QString("Network/API Error (Status %1): %2")
                           .arg(httpStatusCode)
                           .arg(reply->errorString()),
                       httpStatusCode);
  } else {
    QJsonParseError parseError;
    QJsonDocument jsonDoc = QJsonDocument::fromJson(responseData, &parseError);
    if (parseError.error == QJsonParseError::NoError && jsonDoc.isObject()) {
      QJsonObject rootObj = jsonDoc.object();
      if (rootObj.contains("error")) {
        qWarning() << "[Worker Thread] API Error in JSON (Page"
                   << m_requestedPage << "):" << rootObj["message"].toString();
        emit errorOccurred(
            "Last.fm API Error: " + rootObj["message"].toString(), 0);
      } else if (rootObj.contains("recenttracks")) {
        QJsonObject recentTracksObj = rootObj["recenttracks"].toObject();
        QJsonObject attr = recentTracksObj["@attr"].toObject();
        totalPages = attr["totalPages"].toString().toInt();
        currentPage = attr["page"].toString().toInt();
        if (currentPage != m_requestedPage && m_requestedPage > 0) {
          qWarning() << "[Worker] Page mismatch Req:" << m_requestedPage
                     << "Rcv:" << currentPage;
        }

        QJsonArray tracksArray = recentTracksObj["track"].toArray();
        int tracksParsedOnPage = 0;
        for (const QJsonValue &value : tracksArray) {
          QJsonObject trackObj = value.toObject();
          if (trackObj.contains("@attr") &&
              trackObj["@attr"].toObject()["nowplaying"].toString() == "true")
            continue;
          if (trackObj.contains("date")) {
            qint64 uts =
                trackObj["date"].toObject()["uts"].toString().toLongLong();
            if (uts > 0) {
              ScrobbleData scrobble;
              scrobble.artist =
                  trackObj["artist"].toObject()["#text"].toString();
              scrobble.track = trackObj["name"].toString();
              scrobble.album = trackObj["album"].toObject()["#text"].toString();
              scrobble.timestamp = QDateTime::fromSecsSinceEpoch(uts, Qt::UTC);
              fetchedScrobbles.append(scrobble);
              tracksParsedOnPage++;
            } else {
              qWarning() << "[Worker] Invalid UTS <= 0";
            }
          } else {
            qWarning() << "[Worker] Track missing date object";
          }
        }
        qInfo() << "[Worker Thread] Successful Response: Page" << currentPage
                << "/" << totalPages << "| Parsed:" << tracksParsedOnPage;
        emit resultReady(fetchedScrobbles, totalPages, currentPage);
      } else {
        emit errorOccurred("Invalid JSON structure (page " +
                               QString::number(m_requestedPage) + ")",
                           httpStatusCode);
      }
    } else {
      emit errorOccurred("Failed to parse JSON (page " +
                             QString::number(m_requestedPage) + ")",
                         httpStatusCode);
    }
  }
  reply->deleteLater();
  emit finished();
}
