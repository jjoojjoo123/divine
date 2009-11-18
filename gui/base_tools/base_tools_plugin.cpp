/***************************************************************************
 *   Copyright (C) 2009 by Martin Moracek                                  *
 *   xmoracek@fi.muni.cz                                                   *
 *                                                                         *
 *   DiVinE is free software, distributed under GNU GPL and BSD licences.  *
 *   Detailed licence texts may be found in the COPYING file in the        *
 *   distribution tarball. The tool is a product of the ParaDiSe           *
 *   Laboratory, Faculty of Informatics of Masaryk University.             *
 *                                                                         *
 *   This distribution includes freely redistributable third-party code.   *
 *   Please refer to AUTHORS and COPYING included in the distribution for  *
 *   copyright and licensing details.                                      *
 ***************************************************************************/

#include <QtPlugin>
#include <QAction>
#include <QMenu>

#include "base_tools.h"
#include "mainform.h"
#include "layout.h"

#include "prefs_console.h"
#include "prefs_editor.h"
#include "prefs_simulator.h"
#include "prefs_trace.h"
#include "console.h"
#include "trace.h"
#include "watch.h"
#include "transitions.h"
#include "sequence.h"

// console
const bool defConsoleEFont = true;
const bool defConsoleSysColours = true;
const QColor defConsoleFore = QColor("#000");
const QColor defConsoleBack = QColor("#fff");

// simulator
const bool defSimulatorRandom = true;
const uint defSimulatorSeed = 0;
const uint defSimulatorDelay = 0;
const uint defSimulatorSteps = 10;

// trace
const bool defTraceVars = true;
const bool defTraceVarNames = false;
const bool defTraceProcs = true;
const bool defTraceProcNames = true;
const bool defTraceBufs = true;
const bool defTraceBufNames = false;
const QColor defTraceDeadlock = QColor("#88e");
const QColor defTraceError = QColor("#d55");

void BaseToolsPlugin::install(MainForm * root)
{
  // preferences
  PreferencesPage * page = new ConsolePreferences();
  root->registerPreferences(QObject::tr("IDE"), QObject::tr("Console"), page);

  page = new EditorPreferences();
  root->registerPreferences(QObject::tr("IDE"), QObject::tr("Editor"), page);

  page = new SimulatorPreferences();
  root->registerPreferences(QObject::tr("IDE"), QObject::tr("Simulator"), page);

  page = new TracePreferences();
  root->registerPreferences(QObject::tr("IDE"), QObject::tr("Trace"), page);

  QMenu * menu = root->findChild<QMenu*>("docksMenu");
  QAction * action;
  Q_ASSERT(menu);

  // console
  ConsoleDock * console = new ConsoleDock(root);
  console->setObjectName("consoleDock");
  root->addDockWidget(Qt::BottomDockWidgetArea, console);

  connect(root, SIGNAL(message(const QString &)), console, SLOT(appendText(const QString &)));
  connect(root, SIGNAL(settingsChanged()), console, SLOT(readSettings()));

  action = console->toggleViewAction();
  action->setText(QObject::tr("Log &console"));
  action->setStatusTip(QObject::tr("Toggles the console panel"));
  menu->addAction(action);

  QMenu * tmenu = root->findChild<QMenu*>("toolsMenu");
  Q_ASSERT(menu);
  tmenu->addAction(tr("&Clear Console"), console, SLOT(clear()));

  // watch
  WatchDock * watch = new WatchDock(root);
  watch->setObjectName("watchDock");
  root->addDockWidget(Qt::LeftDockWidgetArea, watch);

  connect(root, SIGNAL(simulatorChanged(SimulatorProxy*)),
          watch, SLOT(setSimulator(SimulatorProxy*)));

  action = watch->toggleViewAction();
  action->setText(QObject::tr("&Watch"));
  action->setStatusTip(QObject::tr("Toggles the watch panel"));
  menu->addAction(action);

  root->layouts()->addWidget(watch, QStringList("debug"));
  root->layouts()->addAction(action, QStringList("debug"));

  // trace
  TraceDock * trace = new TraceDock(root);
  trace->setObjectName("traceDock");
  root->addDockWidget(Qt::BottomDockWidgetArea, trace);

  connect(root, SIGNAL(settingsChanged()), trace, SLOT(readSettings()));
  connect(root, SIGNAL(simulatorChanged(SimulatorProxy*)),
          trace, SLOT(setSimulator(SimulatorProxy*)));

  action = trace->toggleViewAction();
  action->setText(QObject::tr("&Trace stack"));
  action->setStatusTip(QObject::tr("Toggles the trace stack panel"));
  menu->addAction(action);

  root->layouts()->addWidget(trace, QStringList("debug"));
  root->layouts()->addAction(action, QStringList("debug"));

  // transitions
  TransitionDock * trans = new TransitionDock(root);
  trans->setObjectName("transitionDock");
  root->addDockWidget(Qt::RightDockWidgetArea, trans);

  connect(trans, SIGNAL(highlightTransition(int)), root, SIGNAL(highlightTransition(int)));
  connect(root, SIGNAL(simulatorChanged(SimulatorProxy*)),
          trans, SLOT(setSimulator(SimulatorProxy*)));

  action = trans->toggleViewAction();
  action->setText(QObject::tr("&Enabled transitions"));
  action->setStatusTip(QObject::tr("Toggles the enabled transitions panel"));
  menu->addAction(action);

  root->layouts()->addWidget(trans, QStringList("debug"));
  root->layouts()->addAction(action, QStringList("debug"));

  // message sequence chart
  SequenceDock * msc = new SequenceDock(root);
  msc->setObjectName("sequenceDock");
  root->addDockWidget(Qt::RightDockWidgetArea, msc);

  connect(root, SIGNAL(simulatorChanged(SimulatorProxy*)),
          msc, SLOT(setSimulator(SimulatorProxy*)));

  action = msc->toggleViewAction();
  action->setText(QObject::tr("&Sequence diagram"));
  action->setStatusTip(QObject::tr("Toggles the sequence diagram panel"));
  menu->addAction(action);

  root->layouts()->addWidget(msc, QStringList("debug"));
  root->layouts()->addAction(action, QStringList("debug"));
}

Q_EXPORT_PLUGIN2(base_tools, BaseToolsPlugin)
