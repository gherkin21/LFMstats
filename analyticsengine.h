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
/**
 * @struct ListeningStreak
 * @brief Holds the results of listening streak calculations.
 */
struct ListeningStreak {
  int longestStreakDays = 0;  /**< @brief The length of the longest consecutive
                                 day listening streak. */
  QDate longestStreakEndDate; /**< @brief The date (local time) the longest
                                 streak ended. */
  int currentStreakDays =
      0; /**< @brief The length of the current consecutive day listening streak
            (ending today or yesterday). */
  QDate currentStreakStartDate; /**< @brief The date (local time) the current
                                   streak started. */
};
/**
 * @class AnalyticsEngine
 * @brief Performs various calculations and statistical analysis on a list of
 * scrobble data.
 * @details Provides methods to calculate top artists/tracks, find last played
 * times, calculate listening streaks, determine mean scrobbles per day, and
 * analyze listening patterns by time of day or day of week. Assumes input
 * scrobble lists are sorted by timestamp (important for some calculations like
 * first/last date).
 * @inherits QObject
 */
class AnalyticsEngine : public QObject {
  Q_OBJECT
public:
  /**
   * @brief Constructs an AnalyticsEngine instance.
   * @param parent The parent QObject, defaults to nullptr.
   */
  explicit AnalyticsEngine(QObject *parent = nullptr);

  /**
   * @brief Calculates the top artists based on play counts.
   * @param scrobbles The list of scrobble data to analyze.
   * @param count The maximum number of top artists to return. If -1 or 0,
   * returns all artists.
   * @return A SortedCounts list containing pairs of artist names and their play
   * counts, sorted descending by count.
   */
  SortedCounts getTopArtists(const QList<ScrobbleData> &scrobbles,
                             int count = 50);
  /**
   * @brief Calculates the top tracks based on play counts.
   * @param scrobbles The list of scrobble data to analyze.
   * @param count The maximum number of top tracks to return. If -1 or 0,
   * returns all tracks.
   * @return A SortedCounts list containing pairs of track identifiers ("Artist
   * - Track") and their play counts, sorted descending by count.
   */
  SortedCounts getTopTracks(const QList<ScrobbleData> &scrobbles,
                            int count = 50);
  /**
   * @brief Finds the timestamp of the last time a specific track by a specific
   * artist was played.
   * @param scrobbles The list of scrobble data to search (assumed sorted
   * chronologically for efficiency, though not strictly required).
   * @param artist The artist name (case-insensitive).
   * @param track The track name (case-insensitive).
   * @return The QDateTime (UTC) of the last play, or an invalid QDateTime if
   * not found.
   */
  QDateTime findLastPlayed(const QList<ScrobbleData> &scrobbles,
                           const QString &artist, const QString &track);
  /**
   * @brief Calculates the total play count for each artist.
   * @param scrobbles The list of scrobble data to analyze.
   * @return A QMap where keys are artist names and values are their total play
   * counts.
   */
  QMap<QString, int> getArtistPlayCounts(const QList<ScrobbleData> &scrobbles);
  /**
   * @brief Calculates the average number of scrobbles per day within a given
   * date range.
   * @param scrobbles The list of scrobble data to analyze.
   * @param fromUTC The start of the date range (UTC, inclusive).
   * @param toUTC The end of the date range (UTC, exclusive).
   * @return The mean number of scrobbles per day, or 0.0 if the range is
   * invalid or contains no scrobbles.
   */
  double getMeanScrobblesPerDay(const QList<ScrobbleData> &scrobbles,
                                const QDateTime &from, const QDateTime &to);
  /**
   * @brief Gets the timestamp of the earliest scrobble in the list.
   * @param scrobbles The list of scrobble data (assumed sorted by timestamp).
   * @return The QDateTime (UTC) of the first scrobble, or an invalid QDateTime
   * if the list is empty or the first timestamp is invalid.
   */
  QDateTime getFirstScrobbleDate(const QList<ScrobbleData> &scrobbles);
  /**
   * @brief Gets the timestamp of the latest scrobble in the list.
   * @param scrobbles The list of scrobble data (assumed sorted by timestamp).
   * @return The QDateTime (UTC) of the last scrobble, or an invalid QDateTime
   * if the list is empty or the last timestamp is invalid.
   */
  QDateTime getLastScrobbleDate(const QList<ScrobbleData> &scrobbles);
  /**
   * @brief Calculates the total number of scrobbles for each hour of the day
   * (local time).
   * @param scrobbles The list of scrobble data to analyze.
   * @return A QVector<int> of size 24, where index 0 represents 00:00-00:59,
   * index 1 represents 01:00-01:59, etc., containing the total counts for each
   * hour.
   */
  QVector<int> getScrobblesPerHourOfDay(const QList<ScrobbleData> &scrobbles);
  /**
   * @brief Calculates the total number of scrobbles for each day of the week
   * (local time).
   * @param scrobbles The list of scrobble data to analyze.
   * @return A QVector<int> of size 7, where index 0 represents Monday, index 1
   * Tuesday, ..., index 6 Sunday, containing the total counts for each day.
   */
  QVector<int> getScrobblesPerDayOfWeek(const QList<ScrobbleData> &scrobbles);
  /**
   * @brief Calculates the longest and current consecutive day listening
   * streaks.
   * @details A "listening day" is defined by local time dates. The current
   * streak requires listening on the current local date or the previous day.
   * @param scrobbles The list of scrobble data to analyze.
   * @return A ListeningStreak struct containing the calculated streak
   * information.
   */
  ListeningStreak
  calculateListeningStreaks(const QList<ScrobbleData> &scrobbles);

  /**
   * @brief Helper template function to sort a QMap by its values (descending).
   * @tparam T The value type in the map (must be comparable with '>').
   * @param map The QMap to sort.
   * @return A QList of QPair<QString, T> sorted by the T value in descending
   * order.
   */
  template <typename T>
  static QList<QPair<QString, T>> sortMapByValue(const QMap<QString, T> &map);
};

#endif
