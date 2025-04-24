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

class MainWindow : public QMainWindow {
  Q_OBJECT

public:
  explicit MainWindow(QWidget *parent = nullptr);
  ~MainWindow();

private slots:
  void onMenuItemChanged(QListWidgetItem *current, QListWidgetItem *previous);
  void setupUser();
  void fetchNewScrobbles();
  void updateMeanScrobbleCalculation();
  void findLastPlayedTrack();

  void handleSavePageOfScrobbles(const QList<ScrobbleData> &pageScrobbles,
                                 int pageNumber);
  void handleTotalPagesDetermined(int totalPages);
  void handleFetchFinished();
  void handleApiError(const QString &errorString);

  void handlePageSaveComplete(int pageNumber);
  void handlePageSaveFailed(int pageNumber, const QString &error);
  void handleDbLoadComplete(const QList<ScrobbleData> &scrobbles);
  void handleDbLoadError(const QString &error);
  void handleDbStatusUpdate(const QString &message);

  void updateDisplay();
  void updateArtistsView();
  void updateTracksView();
  void updateDatabaseTableView();
  void updateChartsView();
  void updateAboutView();
  void updateGeneralStatsView();

private:
  void setupPages();
  void loadDataForCurrentView();
  void setupMenu();
  void promptForSettings();
  void checkOverallCompletion();

  void updateArtistChart();
  void updateTrackChart();
  void updateHourlyChart();
  void updateWeeklyChart();

  Ui::MainWindow *ui;
  SettingsManager m_settingsManager;
  LastFmManager m_lastFmManager;
  DatabaseManager m_databaseManager;
  AnalyticsEngine m_analyticsEngine;

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
  bool m_isLoading = false;
  bool m_fetchingComplete = false;
  int m_expectedTotalPages = 0;
  int m_lastSuccessfullySavedPage = 0;
};
#endif // MAINWINDOW_H
