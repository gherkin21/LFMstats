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

struct SaveWorkItem {
  int pageNumber;
  QString username;
  QList<ScrobbleData> data;
};

class DatabaseManager : public QObject {
  Q_OBJECT
public:
  explicit DatabaseManager(const QString &basePath = "db",
                           QObject *parent = nullptr);
  ~DatabaseManager() override = default;

  void saveScrobblesAsync(int pageNumber, const QString &username,
                          const QList<ScrobbleData> &scrobbles);

  void loadScrobblesAsync(const QString &username, const QDateTime &from,
                          const QDateTime &to);
  void loadAllScrobblesAsync(const QString &username);
  qint64 getLastSyncTimestamp(const QString &username);

  bool isSaveInProgress() const;

signals:
  void pageSaveCompleted(int pageNumber);
  void pageSaveFailed(int pageNumber, const QString &error);

  void loadComplete(const QList<ScrobbleData> &scrobbles);
  void loadError(const QString &error);

  void statusMessage(const QString &message);

private slots:
  void handleLoadFinished();

private:
  void startSaveTaskIfNotRunning();
  void saveTaskLoop();

  static bool saveChunkSync(const QString &basePath, const QString &username,
                            const QList<ScrobbleData> &scrobbles,
                            QString &errorMsg);
  static QDateTime getWeekStart(const QDateTime timestamp);
  static QString getWeekFilePath(const QString &userPath,
                                 const QDateTime timestamp);

  static QList<ScrobbleData> loadScrobblesSync(const QString &basePath,
                                               const QString &username,
                                               const QDateTime &from,
                                               const QDateTime &to,
                                               QString &errorMsg);
  static QList<ScrobbleData> loadAllScrobblesSync(const QString &basePath,
                                                  const QString &username,
                                                  QString &errorMsg);
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
