#ifndef LASTFMMANAGER_H
#define LASTFMMANAGER_H

#include "scrobbledata.h"
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QString>
#include <QThread>
#include <QTimer>
/**
 * @class LastFmManager
 * @brief Manages interaction with the Last.fm API for fetching scrobble data.
 * @details Handles asynchronous fetching of recent tracks, pagination, rate
 * limiting (via delays), and error handling including retries for specific
 * errors. Uses a LastFmWorker object running in a separate thread for network
 * requests.
 * @inherits QObject
 */
class LastFmWorker;

class LastFmManager : public QObject {
  Q_OBJECT
  /** @brief Maximum number of retries for HTTP 500 errors. */
  static const int MAX_500_RETRIES = 3;
  /** @brief Delay between retry attempts in milliseconds (e.g., 60 seconds). */
  static const int RETRY_DELAY_MS = 60 * 1000;

public:
  /**
   * @brief Constructs a LastFmManager instance.
   * @details Initializes the worker thread and worker object, sets up
   * connections, and starts the worker thread.
   * @param parent The parent QObject, defaults to nullptr.
   */
  explicit LastFmManager(QObject *parent = nullptr);
  /**
   * @brief Destructor.
   * @details Stops the worker thread gracefully.
   */
  ~LastFmManager();
  /**
   * @brief Sets the API key and username required for Last.fm API requests.
   * @param apiKey The Last.fm API key.
   * @param username The Last.fm username.
   */
  void setup(const QString &apiKey, const QString &username);
  /**
   * @brief Initiates fetching scrobbles added since a specific timestamp
   * (update mode).
   * @details Resets fetch state and starts fetching from page 1, using the
   * 'from' parameter in the API call.
   * @param lastSyncTimestamp The UTC timestamp (seconds since epoch) of the
   * last known scrobble. Fetches tracks *after* this time.
   */
  void fetchScrobblesSince(qint64 lastSyncTimestamp);
  /**
   * @brief Initiates fetching all scrobbles (initial sync) or resumes an
   * incomplete fetch.
   * @details Starts fetching from the specified page. If knownTotalPages is
   * greater than 0, it uses that value initially.
   * @param startPage The page number to start fetching from (1-based).
   * @param knownTotalPages The previously known total number of pages, or 0 if
   * unknown.
   */
  void startInitialOrResumeFetch(int startPage, int knownTotalPages);

signals:
  /**
   * @brief Internal signal to request the worker thread to perform a fetch
   * operation.
   * @param apiKey The API key to use.
   * @param username The username to fetch for.
   * @param fromTimestamp The 'from' timestamp for the API query (0 for initial
   * fetch).
   * @param page The page number to fetch.
   */
  void startFetching(const QString &apiKey, const QString &username,
                     qint64 fromTimestamp, int page);
  /**
   * @brief Emitted when a page of scrobbles has been successfully fetched and
   * parsed.
   * @param pageScrobbles The list of scrobbles from the fetched page.
   * @param pageNumber The page number that was fetched.
   */
  void pageReadyForSaving(const QList<ScrobbleData> &pageScrobbles,
                          int pageNumber);
  /**
   * @brief Emitted when the total number of pages is determined or updated from
   * an API response.
   * @param totalPages The total number of pages reported by the API.
   */
  void totalPagesDetermined(int totalPages);
  /**
   * @brief Emitted when the fetch process completes, either successfully or
   * after an unrecoverable error.
   */
  void fetchFinished();
  /**
   * @brief Emitted when an error occurs during the fetch process that cannot be
   * automatically recovered from.
   * @param errorString A description of the error.
   */
  void fetchError(const QString &errorString);

private slots:
  /**
   * @brief Slot to handle the results when the worker thread successfully
   * fetches a page.
   * @param pageScrobbles The scrobbles from the page.
   * @param totalPages The total pages reported by the API for this request.
   * @param currentPage The page number that was actually fetched (as reported
   * by API).
   */
  void handlePageResultReady(const QList<ScrobbleData> &pageScrobbles,
                             int totalPages, int currentPage);
  /**
   * @brief Slot to handle errors reported by the worker thread.
   * @param errorString Description of the error.
   * @param httpStatusCode The HTTP status code associated with the error (0 if
   * not applicable).
   */
  void handleFetchErrorWorker(const QString &errorString, int httpStatusCode);
  /**
   * @brief Slot connected to the worker's finished signal. (Currently minimal
   * use).
   */
  void handleWorkerFinished();
  /**
   * @brief Slot called by the retry timer to re-attempt the last failed fetch
   * operation.
   */
  void retryLastFetch();

private:
  /**
   * @brief Schedules the fetch for the next page after a short delay.
   */
  void scheduleNextPageFetch();
  /**
   * @brief Resets retry-related state variables (count, timer, flags).
   */
  void resetRetryState();

  QThread *m_workerThread =
      nullptr; /**< @brief The thread where the LastFmWorker runs. */
  LastFmWorker *m_worker =
      nullptr; /**< @brief The worker object performing network requests. */

  QString m_apiKey;   /**< @brief Stored Last.fm API key. */
  QString m_username; /**< @brief Stored Last.fm username. */
  qint64 m_fetchFromTimestamp =
      0; /**< @brief 'from' timestamp for update fetches (0 otherwise). */
  int m_currentPageFetching =
      0; /**< @brief The page number currently being fetched or requested. */
  int m_expectedTotalPages =
      0; /**< @brief The last known total number of pages. */
  bool m_isPerformingUpdate = false; /**< @brief Flag indicating if the current
                                        fetch is an update (since timestamp). */

  QTimer *m_retryTimer = nullptr; /**< @brief Timer to delay retry attempts. */
  int m_retryCount =
      0; /**< @brief Current retry attempt count for 500 errors. */
  qint64 m_lastFailed_fromTimestamp =
      0; /**< @brief 'from' timestamp of the last failed request. */
  int m_lastFailed_page =
      0; /**< @brief Page number of the last failed request. */
  bool m_isRetryPending =
      false; /**< @brief Flag indicating if a retry attempt is scheduled. */
};

/**
 * @class LastFmWorker
 * @brief Worker object performing actual Last.fm API requests in a separate
 * thread.
 * @details Encapsulates the QNetworkAccessManager and handles the request
 * construction, sending, and basic reply processing (parsing JSON, extracting
 * data/errors). Runs within the thread managed by LastFmManager.
 * @inherits QObject
 */
class LastFmWorker : public QObject {
  Q_OBJECT
public:
  /**
   * @brief Constructs a LastFmWorker instance.
   * @param parent The parent QObject, defaults to nullptr.
   */
  explicit LastFmWorker(QObject *parent = nullptr);
public slots:
  /**
   * @brief Performs the network request to fetch a specific page of recent
   * tracks.
   * @note This slot is executed in the worker thread.
   * @param apiKey The Last.fm API key.
   * @param username The Last.fm username.
   * @param fromTimestamp The 'from' timestamp (seconds since epoch + 1) for
   * update fetches, or 0 for initial/full fetches.
   * @param page The 1-based page number to fetch.
   */
  void doFetch(const QString &apiKey, const QString &username,
               qint64 fromTimestamp, int page);
signals:
  /**
   * @brief Emitted when a page is successfully fetched and parsed.
   * @param scrobbles The list of ScrobbleData extracted from the page.
   * @param totalPages The total number of pages reported by the API response.
   * @param currentPage The page number reported by the API response.
   */
  void resultReady(const QList<ScrobbleData> &scrobbles, int totalPages,
                   int currentPage);
  /**
   * @brief Emitted when a network or API error occurs during the fetch attempt.
   * @param errorString A description of the error.
   * @param httpStatusCode The HTTP status code (e.g., 404, 500), or 0 if not an
   * HTTP error.
   */
  void errorOccurred(const QString &errorString, int httpStatusCode);
  /**
   * @brief Emitted when the processing for a single fetch request (doFetch
   * call) is finished, regardless of success or failure.
   */
  void finished();
private slots:
  /**
   * @brief Slot connected to the QNetworkReply's finished signal.
   * @details Reads the response, parses JSON, extracts data or errors, and
   * emits appropriate signals (resultReady or errorOccurred).
   * @param reply The QNetworkReply that has finished.
   */
  void onReplyFinished(QNetworkReply *reply);

private:
  QNetworkAccessManager
      m_networkManager; /**< @brief Manages network requests for this worker. */
  const QString API_BASE_URL =
      "http://ws.audioscrobbler.com/2.0/"; /**< @brief Base URL for Last.fm API
                                              v2. */
  const int FETCH_LIMIT = 200; /**< @brief Number of tracks to request per page
                                  (max allowed by API). */
  int m_requestedPage =
      0; /**< @brief The page number requested in the current doFetch call. */
  qint64 m_requestedFromTimestamp =
      0; /**< @brief The 'from' timestamp used in the current doFetch call. */
};

#endif // LASTFMMANAGER_H
