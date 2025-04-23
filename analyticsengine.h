#ifndef ANALYTICSENGINE_H
#define ANALYTICSENGINE_H

#include <QObject>
#include <QList>
#include <QMap>
#include <QPair>
#include <QDateTime>
#include <QVector> // For fixed-size vectors
#include <QDate>   // For streak calculation
#include "scrobbledata.h" // Include data structure definition

// Type alias for sorted results (e.g., Name, Count)
using CountPair = QPair<QString, int>;
using SortedCounts = QList<CountPair>;

// Structure to hold listening streak information
struct ListeningStreak {
    int longestStreakDays = 0;     // Length of the longest streak
    QDate longestStreakEndDate;    // Date the longest streak ended
    int currentStreakDays = 0;     // Length of the current streak (if active today/yesterday)
    QDate currentStreakStartDate;  // Date the current streak started (if currentStreakDays > 0)
};


class AnalyticsEngine : public QObject {
    Q_OBJECT
public:
    explicit AnalyticsEngine(QObject *parent = nullptr);

    // --- Calculation Methods ---
    // Returns a list of artists sorted by play count, limited by 'count' (0 or -1 for all).
    SortedCounts getTopArtists(const QList<ScrobbleData> &scrobbles, int count = 50);
    // Returns a list of tracks ("Artist - Track") sorted by play count, limited by 'count'.
    SortedCounts getTopTracks(const QList<ScrobbleData> &scrobbles, int count = 50);
    // Finds the most recent timestamp for a specific track by a specific artist. Returns null QDateTime if not found.
    QDateTime findLastPlayed(const QList<ScrobbleData> &scrobbles, const QString &artist, const QString &track);
    // Returns an unsorted map of artist names to their total play counts.
    QMap<QString, int> getArtistPlayCounts(const QList<ScrobbleData> &scrobbles);
    // Calculates the average number of scrobbles per day within a given date range.
    double getMeanScrobblesPerDay(const QList<ScrobbleData> &scrobbles, const QDateTime &from, const QDateTime &to);
    // Gets the earliest timestamp from the list. Returns invalid QDateTime if list is empty.
    QDateTime getFirstScrobbleDate(const QList<ScrobbleData> &scrobbles);
    // Gets the latest timestamp from the list. Returns invalid QDateTime if list is empty.
    QDateTime getLastScrobbleDate(const QList<ScrobbleData> &scrobbles);
    // Counts scrobbles per hour of the day (0-23). Returns vector of 24 integers.
    QVector<int> getScrobblesPerHourOfDay(const QList<ScrobbleData> &scrobbles);
    // Counts scrobbles per day of the week (0=Mon, 6=Sun). Returns vector of 7 integers.
    QVector<int> getScrobblesPerDayOfWeek(const QList<ScrobbleData> &scrobbles);
    // Calculates longest and current consecutive day listening streaks.
    ListeningStreak calculateListeningStreaks(const QList<ScrobbleData> &scrobbles);


    // --- Public Static Helper ---
    // Helper template function to sort a QMap<QString, T> by its value (descending).
    template <typename T>
    static QList<QPair<QString, T>> sortMapByValue(const QMap<QString, T> &map);

    // private: // No private members currently needed
};

#endif // ANALYTICSENGINE_H
