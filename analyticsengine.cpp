/**
 * @file analyticsengine.cpp
 * @brief Implementation of the AnalyticsEngine class.
 */

#include "analyticsengine.h"
#include <QDebug>
#include <QSet>
#include <algorithm>
#include <limits>

AnalyticsEngine::AnalyticsEngine(QObject *parent) : QObject(parent) {}

template <typename T>
QList<QPair<QString, T>>
AnalyticsEngine::sortMapByValue(const QMap<QString, T> &map) {
  QList<QPair<QString, T>> list;
  list.reserve(map.size());
  for (auto it = map.constBegin(); it != map.constEnd(); ++it) {
    list.append(qMakePair(it.key(), it.value()));
  }

  std::sort(list.begin(), list.end(),
            [](const QPair<QString, T> &a, const QPair<QString, T> &b) {
              return a.second > b.second; // '>' for descending sort
            });

  return list;
}

SortedCounts
AnalyticsEngine::getTopArtists(const QList<ScrobbleData> &scrobbles,
                               int count) {
  QMap<QString, int> artistCounts;
  for (const ScrobbleData &s : scrobbles) {
    artistCounts[s.artist]++;
  }
  SortedCounts sortedList = sortMapByValue(artistCounts);
  if (count > 0 && sortedList.size() > count) {
    return sortedList.mid(0, count);
  }
  return sortedList;
}

SortedCounts AnalyticsEngine::getTopTracks(const QList<ScrobbleData> &scrobbles,
                                           int count) {
  QMap<QString, int> trackCounts;
  for (const ScrobbleData &s : scrobbles) {
    trackCounts[QString("%1 - %2").arg(s.artist, s.track)]++;
  }
  SortedCounts sortedList = sortMapByValue(trackCounts);
  if (count > 0 && sortedList.size() > count) {
    return sortedList.mid(0, count);
  }
  return sortedList;
}

QDateTime AnalyticsEngine::findLastPlayed(const QList<ScrobbleData> &scrobbles,
                                          const QString &artist,
                                          const QString &track) {
  QDateTime lastPlayed;
  for (int i = scrobbles.size() - 1; i >= 0; --i) {
    const ScrobbleData &s = scrobbles[i];
    if (s.artist.compare(artist, Qt::CaseInsensitive) == 0 &&
        s.track.compare(track, Qt::CaseInsensitive) == 0) {
      lastPlayed = s.timestamp;
      break;
    }
  }
  return lastPlayed;
}

QMap<QString, int>
AnalyticsEngine::getArtistPlayCounts(const QList<ScrobbleData> &scrobbles) {
  QMap<QString, int> artistCounts;
  for (const ScrobbleData &s : scrobbles) {
    artistCounts[s.artist]++;
  }
  return artistCounts;
}

double
AnalyticsEngine::getMeanScrobblesPerDay(const QList<ScrobbleData> &scrobbles,
                                        const QDateTime &fromUTC,
                                        const QDateTime &toUTC) {
  if (scrobbles.isEmpty() || !fromUTC.isValid() || !toUTC.isValid() ||
      fromUTC >= toUTC) {
    return 0.0;
  }
  int countInRange = 0;
  for (const auto &s : scrobbles) {
    if (s.timestamp.isValid() && s.timestamp >= fromUTC &&
        s.timestamp < toUTC) {
      countInRange++;
    }
  }
  if (countInRange == 0)
    return 0.0;
  qint64 secondsInRange = fromUTC.secsTo(toUTC);
  if (secondsInRange <= 0)
    return 0.0;
  double daysInRange =
      static_cast<double>(secondsInRange) / (60.0 * 60.0 * 24.0);
  if (daysInRange == 0.0)
    return static_cast<double>(countInRange);
  return static_cast<double>(countInRange) / daysInRange;
}

QDateTime
AnalyticsEngine::getFirstScrobbleDate(const QList<ScrobbleData> &scrobbles) {
  if (scrobbles.isEmpty()) {
    return QDateTime();
  }
  if (scrobbles.first().timestamp.isValid()) {
    return scrobbles.first().timestamp;
  } else {
    QDateTime minDate;
    return minDate;
  }
}

QDateTime
AnalyticsEngine::getLastScrobbleDate(const QList<ScrobbleData> &scrobbles) {
  if (scrobbles.isEmpty()) {
    return QDateTime();
  }
  if (scrobbles.last().timestamp.isValid()) {
    return scrobbles.last().timestamp;
  } else {
    QDateTime maxDate;
    return maxDate;
  }
}

QVector<int> AnalyticsEngine::getScrobblesPerHourOfDay(
    const QList<ScrobbleData> &scrobbles) {
  QVector<int> counts(24, 0);
  for (const auto &s : scrobbles) {
    if (s.timestamp.isValid()) {
      QDateTime localTime = s.timestamp.toLocalTime();
      int hour = localTime.time().hour();
      if (hour >= 0 && hour < 24) {
        counts[hour]++;
      } else {
        qWarning() << "AnalyticsEngine: Invalid local hour found:" << hour;
      }
    }
  }
  return counts;
}

QVector<int> AnalyticsEngine::getScrobblesPerDayOfWeek(
    const QList<ScrobbleData> &scrobbles) {
  QVector<int> counts(7, 0);
  for (const auto &s : scrobbles) {
    if (s.timestamp.isValid()) {
      QDateTime localTime = s.timestamp.toLocalTime();
      int dayOfWeek = localTime.date().dayOfWeek();
      if (dayOfWeek >= 1 && dayOfWeek <= 7) {
        counts[dayOfWeek - 1]++;
      } else {
        qWarning() << "AnalyticsEngine: Invalid local dayOfWeek found:"
                   << dayOfWeek;
      }
    }
  }
  return counts;
}

ListeningStreak AnalyticsEngine::calculateListeningStreaks(
    const QList<ScrobbleData> &scrobbles) {
  ListeningStreak result;
  if (scrobbles.isEmpty()) {
    return result;
  }

  QSet<QDate> listenedDatesLocal;
  for (const auto &s : scrobbles) {
    if (s.timestamp.isValid()) {
      listenedDatesLocal.insert(s.timestamp.toLocalTime().date());
    }
  }

  if (listenedDatesLocal.isEmpty()) {
    return result;
  }

  QList<QDate> sortedDates = listenedDatesLocal.values();
  std::sort(sortedDates.begin(), sortedDates.end());

  int currentStreakLength = 0;
  QDate previousDateLocal;
  for (const QDate currentDateLocal : sortedDates) {
    if (!previousDateLocal.isNull() &&
        previousDateLocal.addDays(1) == currentDateLocal) {
      currentStreakLength++;
    } else {
      currentStreakLength = 1;
    }
    if (currentStreakLength > result.longestStreakDays) {
      result.longestStreakDays = currentStreakLength;
      result.longestStreakEndDate = currentDateLocal;
    }
    previousDateLocal = currentDateLocal;
  }

  QDate todayLocal = QDateTime::currentDateTime().date();
  QDate yesterdayLocal = todayLocal.addDays(-1);
  QDate lastListenedDateLocal = sortedDates.last();

  if (lastListenedDateLocal == todayLocal ||
      lastListenedDateLocal == yesterdayLocal) {
    result.currentStreakDays = 0;
    QDate expectedPrevDate = lastListenedDateLocal;
    for (int i = sortedDates.size() - 1; i >= 0; --i) {
      if (sortedDates[i] == expectedPrevDate) {
        result.currentStreakDays++;
        result.currentStreakStartDate = sortedDates[i];
        expectedPrevDate = expectedPrevDate.addDays(-1);
      } else if (sortedDates[i] < expectedPrevDate) {
        break;
      }
    }
    if (lastListenedDateLocal != todayLocal) {
      result.currentStreakDays = 0;
      result.currentStreakStartDate = QDate();
    }
  } else {
    result.currentStreakDays = 0;
    result.currentStreakStartDate = QDate();
  }

  qDebug() << "Streak Results (Local): Longest=" << result.longestStreakDays
           << "ending" << result.longestStreakEndDate
           << "Current=" << result.currentStreakDays << "starting"
           << result.currentStreakStartDate;

  return result;
}
