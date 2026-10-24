#pragma once
#include <QMainWindow>
#include <QTimer>
#include "Engine.h"

class Timeline;
class StepSequencer;
class MixerPanel;
class QTabWidget;
class QDoubleSpinBox;
class QComboBox;
class QLabel;
class QAction;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow();
private:
    void buildToolbar();
    void setMode(bool easy);
    void tick();
    void importDialog();
    void exportDialog();

    Engine m_engine;
    Timeline* m_timeline;
    StepSequencer* m_seq;
    MixerPanel* m_mixer;
    QTabWidget* m_tabs;
    QAction *m_actPlay, *m_actLoop, *m_actMetro, *m_actSnap;
    QDoubleSpinBox* m_bpm;
    QComboBox *m_mode, *m_preset;
    QLabel* m_time;
    QTimer m_timer;
};
