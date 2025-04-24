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

class LastFmWorker;

class LastFmManager : public QObject {
  Q_OBJECT

  static const int MAX_500_RETRIES = 3;
  static const int RETRY_DELAY_MS = 60 * 1000;

public:
  explicit LastFmManager(QObject *parent = nullptr);
  ~LastFmManager();

  void setup(const QString &apiKey, const QString &username);

  void fetchScrobblesSince(qint64 lastSyncTimestamp);
  void startInitialOrResumeFetch(int startPage, int knownTotalPages);

signals:
  void startFetching(const QString &apiKey, const QString &username,
                     qint64 fromTimestamp, int page);

  void pageReadyForSaving(const QList<ScrobbleData> &pageScrobbles,
                          int pageNumber);
  void totalPagesDetermined(int totalPages);
  void fetchFinished();
  void fetchError(const QString &errorString);

private slots:
  void handlePageResultReady(const QList<ScrobbleData> &pageScrobbles,
                             int totalPages, int currentPage);
  void handleFetchErrorWorker(const QString &errorString, int httpStatusCode);
  void handleWorkerFinished();
  void retryLastFetch();

private:
  void scheduleNextPageFetch();
  void resetRetryState();

  QThread *m_workerThread = nullptr;
  LastFmWorker *m_worker = nullptr;

  QString m_apiKey;
  QString m_username;
  qint64 m_fetchFromTimestamp = 0;
  int m_currentPageFetching = 0;
  int m_expectedTotalPages = 0;
  bool m_isPerformingUpdate = false;

  QTimer *m_retryTimer = nullptr;
  int m_retryCount = 0;
  qint64 m_lastFailed_fromTimestamp = 0;
  int m_lastFailed_page = 0;
  bool m_isRetryPending = false;
};

class LastFmWorker : public QObject {
  Q_OBJECT
public:
  explicit LastFmWorker(QObject *parent = nullptr);
public slots:
  void doFetch(const QString &apiKey, const QString &username,
               qint64 fromTimestamp, int page);
signals:
  void resultReady(const QList<ScrobbleData> &scrobbles, int totalPages,
                   int currentPage);
  void errorOccurred(const QString &errorString, int httpStatusCode);
  void finished();
private slots:
  void onReplyFinished(QNetworkReply *reply);

private:
  QNetworkAccessManager m_networkManager;
  const QString API_BASE_URL = "http://ws.audioscrobbler.com/2.0/";
  const int FETCH_LIMIT = 200;
  int m_requestedPage = 0;
  qint64 m_requestedFromTimestamp = 0;
};

#endif // LASTFMMANAGER_H
