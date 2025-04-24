#ifndef ANALYTICSENGINE_H
#define ANALYTICSENGINE_H

#include "scrobbledata.h"
#include <QDate>
#include <QDateTime>
#include <QList>
#include <QMap>
#include <QObject>
#include <QPair>
#include <QVector>

using CountPair = QPair<QString, int>;
using SortedCounts = QList<CountPair>;

struct ListeningStreak {
  int longestStreakDays = 0;
  QDate longestStreakEndDate;
  int currentStreakDays =
      0;
  QDate currentStreakStartDate;

};

class AnalyticsEngine : public QObject {
  Q_OBJECT
public:
  explicit AnalyticsEngine(QObject *parent = nullptr);

  SortedCounts getTopArtists(const QList<ScrobbleData> &scrobbles,
                             int count = 50);
  SortedCounts getTopTracks(const QList<ScrobbleData> &scrobbles,
                            int count = 50);
  QDateTime findLastPlayed(const QList<ScrobbleData> &scrobbles,
                           const QString &artist, const QString &track);
  QMap<QString, int> getArtistPlayCounts(const QList<ScrobbleData> &scrobbles);
  double getMeanScrobblesPerDay(const QList<ScrobbleData> &scrobbles,
                                const QDateTime &from, const QDateTime &to);
  QDateTime getFirstScrobbleDate(const QList<ScrobbleData> &scrobbles);
  QDateTime getLastScrobbleDate(const QList<ScrobbleData> &scrobbles);
  QVector<int> getScrobblesPerHourOfDay(const QList<ScrobbleData> &scrobbles);
  QVector<int> getScrobblesPerDayOfWeek(const QList<ScrobbleData> &scrobbles);
  ListeningStreak
  calculateListeningStreaks(const QList<ScrobbleData> &scrobbles);

  template <typename T>
  static QList<QPair<QString, T>> sortMapByValue(const QMap<QString, T> &map);
};

#endif
