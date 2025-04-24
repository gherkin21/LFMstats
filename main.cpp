#include "mainwindow.h"

#include "analyticsengine.h"
#include <QApplication>
#include <QMetaType>

/**
 * @brief Application entry point.
 * @details Initializes the QApplication, registers necessary metatypes for
 * cross-thread signals/slots and QVariant storage, creates the main MainWindow
 * instance, shows the window, and starts the application event loop.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return Exit code from QApplication::exec().
 */
int main(int argc, char *argv[]) {
  QApplication a(argc, argv);

  qRegisterMetaType<ListeningStreak>("ListeningStreak");
  qRegisterMetaType<SortedCounts>("SortedCounts");

  MainWindow w;
  w.show();
  return a.exec();
}
