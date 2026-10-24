#pragma once
#include <QWidget>
#include <vector>
#include "Engine.h"

class QScrollBar;
class QSlider;
class QLabel;
class QToolButton;
class QHBoxLayout;

// 스테레오 레벨 미터
class Meter : public QWidget {
public:
    explicit Meter(QWidget* parent = nullptr);
    void setLevel(float l, float r);
protected:
    void paintEvent(QPaintEvent*) override;
private:
    float m_l = 0, m_r = 0;
};

// 16스텝 드럼 시퀀서 (초보자용 핵심 기능)
class StepSequencer : public QWidget {
public:
    StepSequencer(Engine* e, QWidget* parent = nullptr);
protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
private:
    QRectF cell(int r, int s) const;
    bool hit(const QPointF& p, int& r, int& s) const;
    Engine* m_e;
    bool m_drag = false, m_val = false;
    int m_lastR = -1, m_lastS = -1;
};

// 프로용 믹서 (볼륨/팬/뮤트/솔로/미터)
class MixerPanel : public QWidget {
public:
    explicit MixerPanel(Engine* e, QWidget* parent = nullptr);
    void rebuild();
    void tick();
private:
    struct Strip { QWidget* w; QLabel* name; QSlider* vol; QSlider* pan; QToolButton* m; QToolButton* s; Meter* meter; };
    Engine* m_e;
    QWidget* m_host;
    QHBoxLayout* m_lay;
    std::vector<Strip> m_strips;
    QSlider* m_masterVol;
    Meter* m_masterMeter;
};

// 멀티트랙 타임라인
class Timeline : public QWidget {
public:
    explicit Timeline(Engine* e, QWidget* parent = nullptr);
    void setEasyMode(bool on) { m_easy = on; update(); }
    void setSnap(bool on) { m_snap = on; }
    int selectedTrack() const { return m_selTrack; }
    void refreshLayout();
    void followPlayhead();
protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void dragEnterEvent(QDragEnterEvent*) override;
    void dropEvent(QDropEvent*) override;
private:
    static constexpr int kHeaderW = 156, kRulerH = 28, kLaneH = 78;
    double fpb() const { return m_e->framesPerBeat(); }
    double pxPerFrame() const { return m_ppb / fpb(); }
    int frameToX(qint64 f) const;
    qint64 xToFrame(int x) const;
    qint64 snapped(qint64 f) const;
    int laneY(int i) const;
    int laneAt(int y) const;
    QRect muteRect(int i) const;
    QRect soloRect(int i) const;

    Engine* m_e;
    QScrollBar *m_hbar, *m_vbar;
    double m_ppb = 48.0;  // 박자당 픽셀(줌)
    bool m_easy = true, m_snap = true;
    int m_selTrack = -1, m_selClip = -1;
    bool m_dragClip = false, m_dragHead = false, m_dragLoop = false;
    qint64 m_dragOff = 0, m_loopAnchor = 0;
};
