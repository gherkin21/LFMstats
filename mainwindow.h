#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QListWidgetItem>
#include <QStackedWidget>
#include <QTableWidget>
#include <QLabel>
#include <QListWidget>
#include <QLineEdit>    // Include for line edit pointers
#include <QPushButton> // Include for button pointer
#include <QComboBox>   // Include for combobox pointer

// Forward declare UI class generated from mainwindow.ui by UIC
QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

// Include custom classes used by MainWindow
#include "lastfmmanager.h"
#include "databasemanager.h"
#include "analyticsengine.h" // Include header for ListeningStreak struct
#include "settingsmanager.h"
#include "scrobbledata.h"

// Include Qt Charts headers and namespace (required if using charts)
#include <QtCharts/QChartView>
#include <QtCharts/QChartGlobal>

QT_USE_NAMESPACE // Use the Qt Charts namespace

    // Forward declare specific chart types if needed (optional)
    class QChart;
class QBarSeries;
class QLineSeries;

class MainWindow : public QMainWindow {
    Q_OBJECT // Required for classes with signals/slots

public:
    // Constructor and Destructor
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    // --- UI Triggered Slots ---
    void onMenuItemChanged(QListWidgetItem *current, QListWidgetItem *previous);
    void setupUser();
    void fetchNewScrobbles();
    // Slot to update mean scrobbles calculation based on combobox
    void updateMeanScrobbleCalculation();
    // Slot called when the "Find" button for last played is clicked
    void findLastPlayedTrack(); // NEW

    // --- LastFmManager Slots ---
    void handleSavePageOfScrobbles(const QList<ScrobbleData> &pageScrobbles, int pageNumber);
    void handleTotalPagesDetermined(int totalPages);
    void handleFetchFinished();
    void handleApiError(const QString &errorString);

    // --- DatabaseManager Slots ---
    void handlePageSaveComplete(int pageNumber);
    void handlePageSaveFailed(int pageNumber, const QString &error);
    void handleDbLoadComplete(const QList<ScrobbleData> &scrobbles);
    void handleDbLoadError(const QString &error);
    void handleDbStatusUpdate(const QString &message);

    // --- UI Update Slots ---
    void updateDisplay();
    void updateArtistsView();
    void updateTracksView();
    void updateDatabaseTableView();
    void updateChartsView(); // Dispatcher
    void updateAboutView();
    void updateGeneralStatsView();

private:
    // --- Helper Methods ---
    void setupPages();
    void loadDataForCurrentView();
    void setupMenu();
    void promptForSettings();
    void checkOverallCompletion();
    // Chart update helpers
    void updateArtistChart();
    void updateTrackChart();
    void updateHourlyChart();
    void updateWeeklyChart();
    // void updateTimelineChart(); // Optional

    // --- UI and Core Components ---
    Ui::MainWindow *ui;
    SettingsManager m_settingsManager;
    LastFmManager m_lastFmManager;
    DatabaseManager m_databaseManager;
    AnalyticsEngine m_analyticsEngine;

    // --- Pointers to Widgets on Loaded Pages ---
    // Stats Page
    QLabel* m_statsLabel = nullptr; // Maybe remove if unused
    QLabel* m_firstScrobbleLabelValue = nullptr;
    QLabel* m_lastScrobbleLabelValue = nullptr;
    QComboBox* m_meanRangeComboBox = nullptr;
    QLabel* m_meanScrobblesResultLabel = nullptr;
    QLabel* m_longestStreakLabelValue = nullptr; // NEW
    QLabel* m_currentStreakLabelValue = nullptr; // NEW
    QLineEdit* m_artistInput = nullptr;           // NEW
    QLineEdit* m_trackInput = nullptr;            // NEW
    QPushButton* m_findLastPlayedButton = nullptr;// NEW
    QLabel* m_lastPlayedResultLabel = nullptr; // NEW
    // DB Page
    QTableWidget *m_dbTableWidget = nullptr;
    // Artists Page
    QListWidget* m_artistListWidget = nullptr;
    // Tracks Page
    QListWidget* m_trackListWidget = nullptr;
    // Charts Page
    QChartView *m_artistsChartView = nullptr;
    QChartView *m_tracksChartView = nullptr;
    QChartView *m_hourlyChartView = nullptr;
    QChartView *m_weeklyChartView = nullptr;
    // About Page
    QLabel* m_currentUserLabel = nullptr;

    // --- Pointers to the Page Widgets Themselves ---
    QWidget* generalStatsPage = nullptr;
    QWidget* databaseTablePage = nullptr;
    QWidget* artistsPage = nullptr;
    QWidget* tracksPage = nullptr;
    QWidget* chartsPage = nullptr;
    QWidget* aboutPage = nullptr;

    // --- Data & State ---
    QList<ScrobbleData> m_loadedScrobbles;
    bool m_isLoading = false;
    bool m_fetchingComplete = false;
    int m_expectedTotalPages = 0;
    int m_lastSuccessfullySavedPage = 0;
};
#endif // MAINWINDOW_H
