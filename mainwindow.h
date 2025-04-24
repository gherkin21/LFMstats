#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QComboBox>
#include <QFutureWatcher>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMainWindow>
#include <QPushButton>
#include <QStackedWidget>
#include <QTableWidget>
#include <QVariantMap>

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

#include "analyticsengine.h"
#include "databasemanager.h"
#include "lastfmmanager.h"
#include "scrobbledata.h"
#include "settingsmanager.h"

#include <QtCharts/QChartGlobal>
#include <QtCharts/QChartView>

QT_USE_NAMESPACE

class QChart;
class QBarSeries;
class QLineSeries;

using AnalysisResults = QVariantMap;

/**
 * @class MainWindow
 * @brief The main application window for the Last.fm Scrobble Analyzer.
 * @details This class manages the user interface, coordinates interactions
 * between the UI elements, the LastFmManager (for fetching data), the
 * DatabaseManager (for storing/loading data), the SettingsManager (for config),
 * and the AnalyticsEngine (for processing data). It displays various statistics
 * and charts based on the loaded scrobble data. UI responsiveness is maintained
 * by performing database loading and data analysis in background threads.
 * @inherits QMainWindow
 */
class MainWindow : public QMainWindow {
  Q_OBJECT

public:
  /**
   * @brief Constructs the MainWindow.
   * @param parent The parent widget, defaults to nullptr.
   */
  explicit MainWindow(QWidget *parent = nullptr);
  /**
   * @brief Destructor.
   */
  ~MainWindow();

private slots:
  /**
   * @brief Slot called when the selected item in the main menu list changes.
   * @details Switches the view in the stacked widget and triggers data loading
   * or analysis if necessary, ensuring the UI remains responsive.
   * @param current The newly selected list widget item.
   * @param previous The previously selected list widget item.
   */
  void onMenuItemChanged(QListWidgetItem *current, QListWidgetItem *previous);
  /**
   * @brief Slot called to prompt the user for or update Last.fm username and
   * API key settings.
   */
  void setupUser();
  /**
   * @brief Slot called when the user initiates fetching new scrobbles (button
   * click).
   * @details Determines whether to perform an initial fetch/resume or an update
   * fetch based on settings. Updates application state.
   */
  void fetchNewScrobbles();
  /**
   * @brief Slot called when the date range for the mean scrobble calculation
   * changes.
   * @details Recalculates and updates the displayed mean scrobbles per day
   * using loaded data.
   */
  void updateMeanScrobbleCalculation();
  /**
   * @brief Slot called when the user requests to find the last played time for
   * a specific track.
   * @details Reads artist/track input, searches the loaded data using
   * AnalyticsEngine, and updates the result label.
   */
  void findLastPlayedTrack();

  /**
   * @brief Slot to handle a page of scrobbles received from LastFmManager.
   * @details Passes the data to DatabaseManager for asynchronous saving.
   * @param pageScrobbles The list of scrobbles fetched.
   * @param pageNumber The corresponding page number.
   */
  void handleSavePageOfScrobbles(const QList<ScrobbleData> &pageScrobbles,
                                 int pageNumber);
  /**
   * @brief Slot to handle the total number of pages determined by
   * LastFmManager.
   * @details Updates the internal expected total pages count and potentially
   * saves it to settings.
   * @param totalPages The total number of pages reported by the API.
   */
  void handleTotalPagesDetermined(int totalPages);
  /**
   * @brief Slot called when the LastFmManager signals that the fetching process
   * has finished.
   * @details Sets a flag and checks if the overall fetch/save operation is
   * complete using checkOverallCompletion().
   */
  void handleFetchFinished();
  /**
   * @brief Slot to handle errors reported by the LastFmManager.
   * @details Displays an error message to the user and updates application
   * state.
   * @param errorString Description of the API or network error.
   */
  void handleApiError(const QString &errorString);
  /**
   * @brief Slot called when the DatabaseManager signals successful completion
   * of a page save.
   * @details Updates the last successfully saved page number and checks for
   * overall completion using checkOverallCompletion().
   * @param pageNumber The page number that was successfully saved.
   */
  void handlePageSaveComplete(int pageNumber);
  /**
   * @brief Slot called when the DatabaseManager signals failure during a page
   * save.
   * @details Displays an error message and updates state, potentially marking
   * fetch as incomplete. Sets state to Idle.
   * @param pageNumber The page number that failed to save.
   * @param error Description of the save error.
   */
  void handlePageSaveFailed(int pageNumber, const QString &error);
  /**
   * @brief Slot called when the DatabaseManager signals successful loading of
   * scrobbles.
   * @details Stores the loaded data, clears the analysis cache, and triggers
   * the background analysis task via startAnalysisTask().
   * @param scrobbles The list of loaded scrobbles.
   */
  void handleDbLoadComplete(const QList<ScrobbleData> &scrobbles);
  /**
   * @brief Slot called when the DatabaseManager signals an error during data
   * loading.
   * @details Displays an error message to the user, clears data/cache, and sets
   * state to Idle.
   * @param error Description of the load error.
   */
  void handleDbLoadError(const QString &error);
  /**
   * @brief Slot to handle status messages from the DatabaseManager.
   * @details Updates the status bar message, potentially temporarily.
   * @param message The status message text.
   */
  void handleDbStatusUpdate(const QString &message);
  /**
   * @brief Slot called when the background analysis task finishes.
   * @details Retrieves the results from m_analysisWatcher, caches them, updates
   * the UI via updateUiWithAnalysisResults(), and sets the state to Idle.
   */
  void handleAnalysisComplete();
  /**
   * @brief Slot called when the initial database load (triggered by view
   * change) completes.
   * @deprecated This watcher is not currently used; handleDbLoadComplete
   * handles all loads.
   */
  void handleInitialDbLoadComplete();

private:
  /**
   * @enum AppState
   * @brief Defines the possible operational states of the MainWindow.
   */
  enum class AppState {
    Idle, /**< @brief Application is idle, ready for user input or new tasks. */
    LoadingDb,   /**< @brief Currently loading scrobble data from local database
                    files. */
    Analyzing,   /**< @brief Currently processing loaded scrobble data in a
                    background thread. */
    FetchingApi, /**< @brief Actively fetching scrobble data from the Last.fm
                    API. */
    SavingDb     /**< @brief Currently saving fetched scrobble data to local
                    database files. */
  };

  /**
   * @brief Updates the currently visible page/view with data from analysis
   * results.
   * @details Calls the appropriate update*View(results) method based on the
   * active tab.
   * @param results A QVariantMap containing the pre-calculated analysis
   * results.
   */
  void updateUiWithAnalysisResults(const AnalysisResults &results);
  /** @brief Updates the content of the "Top Artists" list view using analysis
   * results. */
  void updateArtistsView(const AnalysisResults &results);
  /** @brief Updates the content of the "Top Tracks" list view using analysis
   * results. */
  void updateTracksView(const AnalysisResults &results);
  /** @brief Updates the content of the "Database View" table using analysis
   * results. */
  void updateDatabaseTableView(const AnalysisResults &results);
  /** @brief Updates all charts on the "Charts" page using analysis results. */
  void updateChartsView(const AnalysisResults &results);
  /** @brief Updates content on the "About / Settings" page (e.g., current
   * user). */
  void updateAboutView();
  /** @brief Updates the various labels and fields on the "Dashboard / Stats"
   * page using analysis results. */
  void updateGeneralStatsView(const AnalysisResults &results);
  /** @brief Updates the Top Artists bar chart using analysis results. */
  void updateArtistChart(const AnalysisResults &results);
  /** @brief Updates the Top Tracks bar chart using analysis results. */
  void updateTrackChart(const AnalysisResults &results);
  /** @brief Updates the Scrobbles per Hour bar chart using analysis results. */
  void updateHourlyChart(const AnalysisResults &results);
  /** @brief Updates the Scrobbles per Day of Week bar chart using analysis
   * results. */
  void updateWeeklyChart(const AnalysisResults &results);

  /** @brief Creates and sets up the individual pages (widgets) within the
   * stacked widget. */
  void setupPages();
  /**
   * @brief Starts the background analysis task if data is loaded and the app is
   * idle.
   * @details Sets the state to Analyzing, runs AnalyticsEngine methods in a
   * separate thread using QtConcurrent, and monitors with m_analysisWatcher.
   */
  void startAnalysisTask();
  /** @brief Populates the main menu list widget. */
  void setupMenu();
  /** @brief Checks if settings (username/API key) are missing and prompts the
   * user if necessary. */
  void promptForSettings();
  /**
   * @brief Checks if both API fetching and database saving are complete after
   * an operation.
   * @details Updates state, potentially triggers database reload and subsequent
   * analysis.
   */
  void checkOverallCompletion();
  /**
   * @brief Updates the status bar text and potentially enables/disables UI
   * elements based on the current application state (m_currentState).
   */
  void updateStatusBarState();

  Ui::MainWindow *ui;
  SettingsManager m_settingsManager;
  LastFmManager m_lastFmManager;
  DatabaseManager m_databaseManager;
  AnalyticsEngine m_analyticsEngine;

  AppState m_currentState = AppState::Idle; /**< @brief The current operational
                                               state of the application. */
  AnalysisResults m_cachedAnalysisResults;  /**< @brief Holds the results of the
                                               last completed analysis to avoid
                                               redundant calculations. */
  QFutureWatcher<AnalysisResults>
      m_analysisWatcher; /**< @brief Monitors the background analysis task. */
  QFutureWatcher<QList<ScrobbleData>>
      m_initialDbLoadWatcher; /**< @brief Monitors initial DB load triggered by
                                 view change (currently unused). */

  QLabel *m_statsLabel = nullptr;
  QLabel *m_firstScrobbleLabelValue = nullptr;
  QLabel *m_lastScrobbleLabelValue = nullptr;
  QComboBox *m_meanRangeComboBox = nullptr;
  QLabel *m_meanScrobblesResultLabel = nullptr;
  QLabel *m_longestStreakLabelValue = nullptr;
  QLabel *m_currentStreakLabelValue = nullptr;
  QLineEdit *m_artistInput = nullptr;
  QLineEdit *m_trackInput = nullptr;
  QPushButton *m_findLastPlayedButton = nullptr;
  QLabel *m_lastPlayedResultLabel = nullptr;

  QTableWidget *m_dbTableWidget = nullptr;

  QListWidget *m_artistListWidget = nullptr;

  QListWidget *m_trackListWidget = nullptr;

  QChartView *m_artistsChartView = nullptr;
  QChartView *m_tracksChartView = nullptr;
  QChartView *m_hourlyChartView = nullptr;
  QChartView *m_weeklyChartView = nullptr;

  QLabel *m_currentUserLabel = nullptr;

  QWidget *generalStatsPage = nullptr;
  QWidget *databaseTablePage = nullptr;
  QWidget *artistsPage = nullptr;
  QWidget *tracksPage = nullptr;
  QWidget *chartsPage = nullptr;
  QWidget *aboutPage = nullptr;

  QList<ScrobbleData> m_loadedScrobbles;
  bool m_fetchingComplete = false;
  int m_expectedTotalPages = 0;
  int m_lastSuccessfullySavedPage = 0;
};
#endif // MAINWINDOW_H
