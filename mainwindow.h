#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMainWindow>
#include <QPushButton>
#include <QStackedWidget>
#include <QTableWidget>

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

/**
 * @class MainWindow
 * @brief The main application window for the Last.fm Scrobble Analyzer.
 * @details This class manages the user interface, coordinates interactions
 * between the UI elements, the LastFmManager (for fetching data), the
 * DatabaseManager (for storing/loading data), the SettingsManager (for config),
 * and the AnalyticsEngine (for processing data). It displays various statistics
 * and charts based on the loaded scrobble data.
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
   * @details Switches the view in the stacked widget and potentially triggers
   * data loading/display update.
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
   * fetch based on settings.
   */
  void fetchNewScrobbles();
  /**
   * @brief Slot called when the date range for the mean scrobble calculation
   * changes.
   * @details Recalculates and updates the displayed mean scrobbles per day.
   */
  void updateMeanScrobbleCalculation();
  /**
   * @brief Slot called when the user requests to find the last played time for
   * a specific track.
   * @details Reads artist/track input, searches the loaded data, and updates
   * the result label.
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
   * complete.
   */
  void handleFetchFinished();
  /**
   * @brief Slot to handle errors reported by the LastFmManager.
   * @details Displays an error message to the user and updates state.
   * @param errorString Description of the API or network error.
   */
  void handleApiError(const QString &errorString);
  /**
   * @brief Slot called when the DatabaseManager signals successful completion
   * of a page save.
   * @details Updates the last successfully saved page number and checks for
   * overall completion.
   * @param pageNumber The page number that was successfully saved.
   */
  void handlePageSaveComplete(int pageNumber);
  /**
   * @brief Slot called when the DatabaseManager signals failure during a page
   * save.
   * @details Displays an error message and updates state, potentially marking
   * fetch as incomplete.
   * @param pageNumber The page number that failed to save.
   * @param error Description of the save error.
   */
  void handlePageSaveFailed(int pageNumber, const QString &error);
  /**
   * @brief Slot called when the DatabaseManager signals successful loading of
   * scrobbles.
   * @details Stores the loaded data and triggers a display update.
   * @param scrobbles The list of loaded scrobbles.
   */
  void handleDbLoadComplete(const QList<ScrobbleData> &scrobbles);
  /**
   * @brief Slot called when the DatabaseManager signals an error during data
   * loading.
   * @details Displays an error message to the user.
   * @param error Description of the load error.
   */
  void handleDbLoadError(const QString &error);
  /**
   * @brief Slot to handle status messages from the DatabaseManager.
   * @details Updates the status bar message.
   * @param message The status message text.
   */
  void handleDbStatusUpdate(const QString &message);

  /**
   * @brief Updates the currently visible page/view with the loaded data.
   * @details Calls the appropriate update*View() method based on the active
   * tab.
   */
  void updateDisplay();
  /** @brief Updates the content of the "Top Artists" list view. */
  void updateArtistsView();
  /** @brief Updates the content of the "Top Tracks" list view. */
  void updateTracksView();
  /** @brief Updates the content of the "Database View" table. */
  void updateDatabaseTableView();
  /** @brief Updates all charts on the "Charts" page. */
  void updateChartsView();
  /** @brief Updates content on the "About / Settings" page (e.g., current
   * user). */
  void updateAboutView();
  /** @brief Updates the various labels and fields on the "Dashboard / Stats"
   * page. */
  void updateGeneralStatsView();

private:
  /** @brief Creates and sets up the individual pages (widgets) within the
   * stacked widget. */
  void setupPages();
  /** @brief Loads data from the database if needed for the currently active
   * view. */
  void loadDataForCurrentView();
  /** @brief Populates the main menu list widget. */
  void setupMenu();
  /** @brief Checks if settings (username/API key) are missing and prompts the
   * user if necessary. */
  void promptForSettings();
  /** @brief Checks if both API fetching and database saving are complete after
   * an operation. Updates state and UI accordingly. */
  void checkOverallCompletion();

  /** @brief Updates the Top Artists bar chart. */
  void updateArtistChart();
  /** @brief Updates the Top Tracks bar chart. */
  void updateTrackChart();
  /** @brief Updates the Scrobbles per Hour bar chart. */
  void updateHourlyChart();
  /** @brief Updates the Scrobbles per Day of Week bar chart. */
  void updateWeeklyChart();

  Ui::MainWindow *ui; /**< @brief Pointer to the Qt Designer UI components. */
  SettingsManager
      m_settingsManager;         /**< @brief Manages application settings. */
  LastFmManager m_lastFmManager; /**< @brief Manages Last.fm API interaction. */
  DatabaseManager m_databaseManager; /**< @brief Manages local data storage. */
  AnalyticsEngine m_analyticsEngine; /**< @brief Performs data analysis. */

  QLabel *m_statsLabel =
      nullptr; /**< @brief General statistics label (potentially unused). */
  QLabel *m_firstScrobbleLabelValue =
      nullptr; /**< @brief Label displaying the date of the first scrobble. */
  QLabel *m_lastScrobbleLabelValue =
      nullptr; /**< @brief Label displaying the date of the last scrobble. */
  QComboBox *m_meanRangeComboBox =
      nullptr; /**< @brief Combobox for selecting the time range for mean
                  calculation. */
  QLabel *m_meanScrobblesResultLabel =
      nullptr; /**< @brief Label displaying the calculated mean scrobbles/day.
                */
  QLabel *m_longestStreakLabelValue =
      nullptr; /**< @brief Label displaying the longest listening streak. */
  QLabel *m_currentStreakLabelValue =
      nullptr; /**< @brief Label displaying the current listening streak. */
  QLineEdit *m_artistInput =
      nullptr; /**< @brief Input field for artist name (Find Last Played). */
  QLineEdit *m_trackInput =
      nullptr; /**< @brief Input field for track name (Find Last Played). */
  QPushButton *m_findLastPlayedButton =
      nullptr; /**< @brief Button to trigger the Find Last Played search. */
  QLabel *m_lastPlayedResultLabel =
      nullptr; /**< @brief Label displaying the result of the Find Last Played
                  search. */

  QTableWidget *m_dbTableWidget =
      nullptr; /**< @brief Table displaying raw database content (e.g., artist
                  counts). */

  QListWidget *m_artistListWidget =
      nullptr; /**< @brief List displaying top artists. */

  QListWidget *m_trackListWidget =
      nullptr; /**< @brief List displaying top tracks. */

  QChartView *m_artistsChartView =
      nullptr; /**< @brief View for displaying the top artists chart. */
  QChartView *m_tracksChartView =
      nullptr; /**< @brief View for displaying the top tracks chart. */
  QChartView *m_hourlyChartView =
      nullptr; /**< @brief View for displaying the hourly scrobble distribution
                  chart. */
  QChartView *m_weeklyChartView =
      nullptr; /**< @brief View for displaying the weekly scrobble distribution
                  chart. */

  QLabel *m_currentUserLabel = nullptr; /**< @brief Label displaying the
                                           currently configured username. */

  QWidget *generalStatsPage =
      nullptr; /**< @brief Widget for the General Stats page. */
  QWidget *databaseTablePage =
      nullptr; /**< @brief Widget for the Database Table page. */
  QWidget *artistsPage =
      nullptr;                   /**< @brief Widget for the Top Artists page. */
  QWidget *tracksPage = nullptr; /**< @brief Widget for the Top Tracks page. */
  QWidget *chartsPage = nullptr; /**< @brief Widget for the Charts page. */
  QWidget *aboutPage =
      nullptr; /**< @brief Widget for the About/Settings page. */

  QList<ScrobbleData>
      m_loadedScrobbles;    /**< @brief Holds the scrobble data currently loaded
                               from the database. */
  bool m_isLoading = false; /**< @brief Flag indicating if a fetch or load
                               operation is currently in progress. */
  bool m_fetchingComplete =
      false; /**< @brief Flag indicating if the API fetching part of an
                operation is complete. */
  int m_expectedTotalPages = 0; /**< @brief The total number of pages expected
                                   during the current fetch operation. */
  int m_lastSuccessfullySavedPage =
      0; /**< @brief The highest page number successfully saved during the
            current fetch operation. */
};
#endif // MAINWINDOW_H
