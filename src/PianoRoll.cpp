// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "PianoRoll.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <cmath>
#include "Theme.h"

/// The drawing surface of the piano roll. It always reads the engine's data and writes edits back.
class RollCanvas : public QWidget {
public:
    RollCanvas(Engine* e, QWidget* parent) : QWidget(parent), m_e(e) {
        setFocusPolicy(Qt::ClickFocus);
        setMinimumHeight(160);
        m_scrollY = (kHi - 76) * kKeyH;
    }

    void setRegion(int t, int r) { m_t = t; m_r = r; m_sel = -1; update(); }
    void setGrid(double g) { m_grid = g; }
    bool hasSelection() const { return m_sel >= 0; }

    void deleteSelected() {
        if (m_sel < 0 || !load()) return;
        m_e->checkpoint();
        m_work.erase(m_work.begin() + m_sel);
        m_sel = -1;
        m_e->setRegionNotes(m_t, m_r, m_work);
        update();
    }

    void quantize() {
        if (!load()) return;
        m_e->checkpoint();
        for (Note& n : m_work) {
            n.start = std::max(0.0, std::round(n.start / m_grid) * m_grid);
            n.length = std::max(m_grid, std::round(n.length / m_grid) * m_grid);
        }
        m_e->setRegionNotes(m_t, m_r, m_work);
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.fillRect(rect(), palette().color(QPalette::Base));
        const auto tr = m_e->snapshot();
        if (m_t < 0 || m_t >= int(tr.size()) || m_r < 0 || m_r >= int(tr[size_t(m_t)].regions.size()) || !tr[size_t(m_t)].regions[size_t(m_r)].midi) {
            p.setPen(theme::kDim);
            p.drawText(rect(), Qt::AlignCenter, tr_("Double-click a MIDI region in the Tracks area to edit its notes."));
            return;
        }
        const Track& trk = tr[size_t(m_t)];
        const Region& rg = trk.regions[size_t(m_r)];
        const auto tm = m_e->tempoMap();
        auto isBar = [&](double beat) { return std::fabs(beat - tm->barStart(tm->barAt(beat))) < 1e-6; };
        const int W = width(), areaBottom = height() - kVelH;

        // Pitch rows.
        p.setClipRect(0, kRulerH, W, areaBottom - kRulerH);
        for (int pitch = kLo; pitch <= kHi; ++pitch) {
            const int y = pitchY(pitch);
            const bool black = isBlack(pitch);
            p.fillRect(kKeyW, y, W - kKeyW, kKeyH, black ? QColor(34, 34, 37) : QColor(44, 44, 48));
            p.setPen(QColor(24, 24, 27));
            p.drawLine(kKeyW, y, W, y);
        }
        // Beat grid.
        const double firstBeat = std::max(0.0, std::floor(m_scrollX / m_ppb / m_grid) * m_grid);
        for (double b = firstBeat; b * m_ppb - m_scrollX < W; b += m_grid) {
            const int x = beatX(b);
            const double absBeat = rg.start + b;
            const bool bar = isBar(absBeat), beat = std::fmod(absBeat, 1.0) < 1e-6;
            p.setPen(QColor(255, 255, 255, bar ? 80 : beat ? 40 : 16));
            p.drawLine(x, kRulerH, x, areaBottom);
        }
        // Region end.
        const int endX = beatX(rg.lengthBeats);
        p.fillRect(endX, kRulerH, std::max(0, W - endX), areaBottom - kRulerH, QColor(0, 0, 0, 90));

        // Notes.
        for (size_t i = 0; i < rg.notes.size(); ++i) {
            const Note& n = rg.notes[i];
            const QRect r(beatX(n.start), pitchY(n.pitch) + 1, std::max(3, int(n.length * m_ppb) - 1), kKeyH - 2);
            QColor c = trk.color.lighter(80 + n.velocity);
            p.setBrush(c);
            p.setPen(int(i) == m_sel ? QPen(Qt::white, 2) : QPen(c.darker(160)));
            p.drawRoundedRect(r, 2, 2);
        }
        // Playhead.
        const int hx = beatX(m_e->positionBeats() - rg.start);
        p.setPen(Qt::white);
        p.drawLine(hx, kRulerH, hx, areaBottom);

        // Piano keys.
        p.setClipRect(0, kRulerH, kKeyW, areaBottom - kRulerH);
        for (int pitch = kLo; pitch <= kHi; ++pitch) {
            const int y = pitchY(pitch);
            const bool black = isBlack(pitch);
            p.fillRect(0, y, black ? kKeyW * 2 / 3 : kKeyW, kKeyH, black ? QColor(25, 25, 28) : QColor(225, 225, 228));
            p.setPen(QColor(60, 60, 66));
            p.drawLine(0, y, kKeyW, y);
            if (pitch % 12 == 0) {
                p.setPen(Qt::black);
                p.drawText(QRect(kKeyW - 28, y, 26, kKeyH), Qt::AlignRight | Qt::AlignVCenter, QString("C%1").arg(pitch / 12 - 2));
            }
        }

        // Ruler.
        p.setClipping(false);
        p.fillRect(0, 0, W, kRulerH, palette().color(QPalette::Window));
        p.setClipRect(kKeyW, 0, W - kKeyW, kRulerH);
        p.setPen(theme::kText);
        for (double b = std::floor(m_scrollX / m_ppb); b * m_ppb - m_scrollX < W; b += 1.0) {
            if (b < 0) continue;
            const double absBeat = rg.start + b;
            const int x = beatX(b);
            const bool bar = isBar(absBeat);
            p.drawLine(x, bar ? 4 : kRulerH - 7, x, kRulerH);
            if (bar) p.drawText(x + 4, 15, QString::number(tm->barAt(absBeat)));
        }

        // Velocity lane.
        p.setClipping(false);
        p.fillRect(0, areaBottom, W, kVelH, QColor(30, 30, 33));
        p.setClipRect(kKeyW, areaBottom, W - kKeyW, kVelH);
        p.setPen(theme::kDim);
        p.drawText(QRect(4, areaBottom + 2, kKeyW - 8, 16), Qt::AlignLeft, tr_("Velocity"));
        for (size_t i = 0; i < rg.notes.size(); ++i) {
            const Note& n = rg.notes[i];
            const int h = int((kVelH - 8) * n.velocity / 127.0);
            p.fillRect(beatX(n.start), areaBottom + kVelH - 4 - h, 5, h, int(i) == m_sel ? QColor(Qt::white) : trk.color);
        }
    }

    void mousePressEvent(QMouseEvent* e) override {
        setFocus();
        if (!load()) return;
        const QPoint pt = e->position().toPoint();
        const int areaBottom = height() - kVelH;
        m_mode = Mode::None;

        if (pt.y() < kRulerH) {   // ruler: seek
            if (pt.x() >= kKeyW) m_e->seekBeats(m_regionStart + std::max(0.0, beatAt(pt.x())));
            return;
        }
        if (pt.x() < kKeyW && pt.y() < areaBottom) {   // piano keys: audition
            m_preview = pitchAt(pt.y());
            m_e->noteOn(m_t, m_preview, 100);
            return;
        }
        if (pt.y() >= areaBottom) {   // velocity lane
            const int hit = noteAtX(pt.x());
            if (hit >= 0) {
                m_e->checkpoint();
                m_sel = hit;
                m_mode = Mode::Velocity;
                setVelocity(pt.y());
            }
            return;
        }
        const int hit = noteAt(pt);
        if (e->button() == Qt::RightButton) {
            if (hit >= 0) { m_sel = hit; deleteSelected(); }
            return;
        }
        m_e->checkpoint();
        if (hit >= 0) {
            m_sel = hit;
            const Note& n = m_work[size_t(hit)];
            m_grab = beatAt(pt.x()) - n.start;
            m_mode = (pt.x() >= beatX(n.start + n.length) - 6) ? Mode::Resize : Mode::Move;
            m_e->noteOn(m_t, n.pitch, n.velocity);
            m_preview = n.pitch;
        } else {
            const double beat = std::floor(beatAt(pt.x()) / m_grid) * m_grid;
            if (beat < 0 || beat >= m_regionLen) return;
            Note n;
            n.pitch = pitchAt(pt.y());
            n.start = beat;
            n.length = m_grid;
            m_work.push_back(n);
            m_sel = int(m_work.size()) - 1;
            m_mode = Mode::Resize;   // dragging right after the click stretches the new note
            m_e->setRegionNotes(m_t, m_r, m_work);
            m_e->noteOn(m_t, n.pitch, n.velocity);
            m_preview = n.pitch;
        }
        update();
    }

    void mouseMoveEvent(QMouseEvent* e) override {
        if (m_sel < 0 || m_sel >= int(m_work.size())) return;
        const QPoint pt = e->position().toPoint();
        Note& n = m_work[size_t(m_sel)];
        if (m_mode == Mode::Move) {
            n.start = std::clamp(std::round((beatAt(pt.x()) - m_grab) / m_grid) * m_grid, 0.0, std::max(0.0, m_regionLen - m_grid));
            const int np = pitchAt(pt.y());
            if (np != n.pitch) {
                m_e->noteOff(m_t, m_preview);
                n.pitch = np;
                m_preview = np;
                m_e->noteOn(m_t, np, n.velocity);
            }
        } else if (m_mode == Mode::Resize) {
            n.length = std::max(m_grid, std::round((beatAt(pt.x()) - n.start) / m_grid) * m_grid);
        } else if (m_mode == Mode::Velocity) {
            setVelocity(pt.y());
            return;
        } else {
            return;
        }
        m_e->setRegionNotes(m_t, m_r, m_work);
        update();
    }

    void mouseReleaseEvent(QMouseEvent*) override {
        if (m_preview >= 0) { m_e->noteOff(m_t, m_preview); m_preview = -1; }
        m_mode = Mode::None;
    }

    void wheelEvent(QWheelEvent* e) override {
        const int dy = e->angleDelta().y();
        if (e->modifiers() & Qt::ControlModifier) {
            const double beatAtCursor = beatAt(int(e->position().x()));
            m_ppb = std::clamp(m_ppb * (dy > 0 ? 1.2 : 1 / 1.2), 20.0, 600.0);
            m_scrollX = std::max(0.0, beatAtCursor * m_ppb - (e->position().x() - kKeyW));
        } else if (e->modifiers() & Qt::ShiftModifier) {
            m_scrollX = std::max(0.0, m_scrollX - dy / 2.0);
        } else {
            const int maxY = (kHi - kLo + 1) * kKeyH - (height() - kVelH - kRulerH);
            m_scrollY = std::clamp(m_scrollY - dy / 2, 0, std::max(0, maxY));
        }
        update();
        e->accept();
    }

    void keyPressEvent(QKeyEvent* e) override {
        if (e->key() == Qt::Key_Q) quantize();
        else QWidget::keyPressEvent(e);
    }

private:
    enum class Mode { None, Move, Resize, Velocity };
    static constexpr int kKeyW = 64, kRulerH = 22, kVelH = 60, kKeyH = 14, kLo = 24, kHi = 108;

    static QString tr_(const char* s) { return QObject::tr(s); }
    static bool isBlack(int pitch) { const int k = pitch % 12; return k == 1 || k == 3 || k == 6 || k == 8 || k == 10; }
    int pitchY(int pitch) const { return kRulerH + (kHi - pitch) * kKeyH - m_scrollY; }
    int pitchAt(int y) const { return std::clamp(kHi - (y - kRulerH + m_scrollY) / kKeyH, kLo, kHi); }
    int beatX(double beat) const { return kKeyW + int(std::lround(beat * m_ppb - m_scrollX)); }
    double beatAt(int x) const { return (x - kKeyW + m_scrollX) / m_ppb; }

    // Copies the current region's notes into m_work; false if no valid MIDI region is selected.
    bool load() {
        const auto tr = m_e->snapshot();
        if (m_t < 0 || m_t >= int(tr.size()) || m_r < 0 || m_r >= int(tr[size_t(m_t)].regions.size())) return false;
        const Region& rg = tr[size_t(m_t)].regions[size_t(m_r)];
        if (!rg.midi) return false;
        m_work = rg.notes;
        m_regionLen = rg.lengthBeats;
        m_regionStart = rg.start;
        return true;
    }

    int noteAt(QPoint pt) const {
        for (int i = int(m_work.size()) - 1; i >= 0; --i) {
            const Note& n = m_work[size_t(i)];
            const QRect r(beatX(n.start), pitchY(n.pitch), std::max(3, int(n.length * m_ppb)), kKeyH);
            if (r.contains(pt)) return i;
        }
        return -1;
    }

    int noteAtX(int x) const {
        for (int i = int(m_work.size()) - 1; i >= 0; --i) {
            const int nx = beatX(m_work[size_t(i)].start);
            if (x >= nx - 2 && x <= nx + 7) return i;
        }
        return -1;
    }

    void setVelocity(int y) {
        const int areaBottom = height() - kVelH;
        m_work[size_t(m_sel)].velocity = std::clamp(int(127.0 * (areaBottom + kVelH - 4 - y) / (kVelH - 8)), 1, 127);
        m_e->setRegionNotes(m_t, m_r, m_work);
        update();
    }

    Engine* m_e;
    int m_t = -1, m_r = -1, m_sel = -1, m_preview = -1, m_scrollY = 0;
    double m_ppb = 90.0, m_scrollX = 0, m_grid = 0.25, m_grab = 0, m_regionLen = 4, m_regionStart = 0;
    Mode m_mode = Mode::None;
    std::vector<Note> m_work;
};

// ------------------------------------------------------------------ PianoRoll
PianoRoll::PianoRoll(Engine* e, QWidget* parent) : QWidget(parent) {
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    auto* bar = new QHBoxLayout;
    bar->setContentsMargins(6, 4, 6, 4);
    bar->addWidget(new QLabel(tr("Grid")));
    m_grid = new QComboBox;
    m_grid->addItems({"1/4", "1/8", "1/16", "1/32"});
    m_grid->setCurrentIndex(2);
    bar->addWidget(m_grid);
    auto* quant = new QPushButton(tr("Quantize (Q)"));
    bar->addWidget(quant);
    bar->addWidget(new QLabel(tr("Click to draw · drag edge to resize · right-click or Delete to remove")));
    bar->addStretch(1);
    lay->addLayout(bar);
    m_canvas = new RollCanvas(e, this);
    lay->addWidget(m_canvas, 1);

    connect(m_grid, &QComboBox::currentIndexChanged, this, [this](int i) {
        static const double g[] = {1.0, 0.5, 0.25, 0.125};
        m_canvas->setGrid(g[std::clamp(i, 0, 3)]);
    });
    connect(quant, &QPushButton::clicked, this, [this] { m_canvas->quantize(); });
    m_canvas->setGrid(0.25);
}

void PianoRoll::setRegion(int track, int region) { m_canvas->setRegion(track, region); }
void PianoRoll::deleteSelectedNote() { m_canvas->deleteSelected(); }
bool PianoRoll::hasSelectedNote() const { return m_canvas->hasSelection(); }
