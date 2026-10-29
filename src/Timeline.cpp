// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Timeline.h"

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QInputDialog>
#include <QLineEdit>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QUrl>
#include <QWheelEvent>
#include <cmath>
#include "Theme.h"

namespace {
/// Small round icon that tells the track types apart, like the icons in Logic's track headers.
void drawTrackGlyph(QPainter& p, const QRect& r, TrackType type, const QColor& color) {
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(color.darker(130));
    p.drawEllipse(r);
    p.setBrush(Qt::white);
    const QPointF c = r.center();
    const double u = r.width() / 24.0;
    switch (type) {
    case TrackType::Audio:   // waveform bars
        for (int i = -2; i <= 2; ++i) {
            const double h = (i == 0 ? 9 : std::abs(i) == 1 ? 6 : 3) * u;
            p.drawRoundedRect(QRectF(c.x() + i * 3.6 * u - 1.1 * u, c.y() - h, 2.2 * u, 2 * h), u, u);
        }
        break;
    case TrackType::Instrument:   // piano keys
        p.drawRoundedRect(QRectF(c.x() - 8 * u, c.y() - 6 * u, 16 * u, 12 * u), 2 * u, 2 * u);
        p.setBrush(color.darker(130));
        for (int i = -1; i <= 1; i += 2) p.drawRect(QRectF(c.x() + i * 2.6 * u - 1 * u, c.y() - 6 * u, 2 * u, 7 * u));
        break;
    case TrackType::Drums:   // two drum heads
        p.drawEllipse(QPointF(c.x() - 4.5 * u, c.y() + 1 * u), 4 * u, 3 * u);
        p.drawEllipse(QPointF(c.x() + 4.5 * u, c.y() - 1 * u), 4 * u, 3 * u);
        break;
    case TrackType::Drummer:   // drum with sticks
        p.drawEllipse(QPointF(c.x(), c.y() + 3 * u), 7 * u, 4 * u);
        p.setPen(QPen(Qt::white, 1.8 * u, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(c.x() - 6 * u, c.y() - 7 * u), QPointF(c.x() - 1 * u, c.y() - 1 * u));
        p.drawLine(QPointF(c.x() + 6 * u, c.y() - 7 * u), QPointF(c.x() + 1 * u, c.y() - 1 * u));
        break;
    }
    p.restore();
}
}  // namespace

Timeline::Timeline(Engine* e, QWidget* p) : QWidget(p), m_e(e) {
    setFocusPolicy(Qt::StrongFocus);
    setAcceptDrops(true);
    setMouseTracking(true);
    setMinimumHeight(200);
    m_hbar = new QScrollBar(Qt::Horizontal, this);
    m_vbar = new QScrollBar(Qt::Vertical, this);
    connect(m_hbar, &QScrollBar::valueChanged, this, [this] { update(); });
    connect(m_vbar, &QScrollBar::valueChanged, this, [this] { update(); });
}

// ---------------------------------------------------------------- coordinates
int Timeline::beatToX(double beat) const { return kHeaderW + int(std::lround(beat * m_ppb)) - m_hbar->value(); }
double Timeline::xToBeat(int x) const { return (x - kHeaderW + m_hbar->value()) / m_ppb; }

double Timeline::gridBeats() const {   // "smart" grid: the finest division that is still >= 12 px wide
    for (double g : {0.25, 0.5, 1.0, 2.0})
        if (g * m_ppb >= 12) return g;
    return 4.0;
}

double Timeline::snapBeat(double beat) const {
    if (!m_snap) return std::max(0.0, beat);
    const double g = gridBeats();
    return std::max(0.0, std::round(beat / g) * g);
}

int Timeline::laneAt(int y) const {
    if (y < lanesTop()) return -1;
    return (y - lanesTop() + m_vbar->value()) / kLaneH;
}

QRect Timeline::muteRect(int i) const { return QRect(46, laneY(i) + kLaneH - 28, 24, 18); }
QRect Timeline::soloRect(int i) const { return QRect(74, laneY(i) + kLaneH - 28, 24, 18); }
QRect Timeline::armRect(int i) const { return QRect(102, laneY(i) + kLaneH - 28, 24, 18); }

float Timeline::yToVolume(int y, int lane) const {
    const int top = laneY(lane) + 6, bottom = laneY(lane) + kLaneH - 6;
    return std::clamp(1.5f * (1.f - float(y - top) / float(bottom - top)), 0.f, 1.5f);
}

int Timeline::volumeToY(float v, int lane) const {
    const int top = laneY(lane) + 6, bottom = laneY(lane) + kLaneH - 6;
    return bottom - int(v / 1.5f * float(bottom - top));
}

int Timeline::bpmToY(double bpm) const {   // tempo lane scale: 40 .. 240 bpm
    const double f = std::clamp((bpm - 40.0) / 200.0, 0.0, 1.0);
    return kRulerH + kTempoH - 8 - int(f * (kTempoH - 16));
}

double Timeline::yToBpm(int y) const {
    const double f = 1.0 - double(y - (kRulerH + 8)) / double(kTempoH - 16);
    return std::clamp(std::round(40.0 + f * 200.0), 30.0, 300.0);
}

int Timeline::tempoPointAt(const std::vector<dsp::TempoPoint>& pts, QPoint pt) const {
    for (size_t i = 0; i < pts.size(); ++i)
        if (std::abs(beatToX(pts[i].beat) - pt.x()) <= 7 && std::abs(bpmToY(pts[i].bpm) - pt.y()) <= 7) return int(i);
    return -1;
}

bool Timeline::hitRegion(const std::vector<Track>& tr, QPoint pt, int& t, int& r, bool& edge) const {
    t = laneAt(pt.y());
    if (t < 0 || t >= int(tr.size()) || pt.x() < kHeaderW) return false;
    const auto& regs = tr[size_t(t)].regions;
    for (int i = int(regs.size()) - 1; i >= 0; --i) {
        const int x0 = beatToX(regs[size_t(i)].start), x1 = beatToX(regs[size_t(i)].start + m_e->regionBeats(regs[size_t(i)]));
        if (pt.x() >= x0 && pt.x() <= x1) {
            r = i;
            edge = pt.x() >= x1 - 5;
            return true;
        }
    }
    return false;
}

void Timeline::refreshLayout() {
    const int W = width() - m_vbar->width(), H = height() - m_hbar->height();
    const double endBeats = std::max(64.0, m_e->projectEndBeats() + 16);
    m_hbar->setRange(0, std::max(0, int(endBeats * m_ppb) - (W - kHeaderW)));
    m_hbar->setPageStep(std::max(1, W - kHeaderW));
    m_vbar->setRange(0, std::max(0, lanesTop() + m_e->trackCount() * kLaneH - H));
    m_vbar->setPageStep(std::max(1, H - lanesTop()));
    update();
}

void Timeline::followPlayhead() {
    const int W = width() - m_vbar->width();
    const int x = beatToX(m_e->positionBeats());
    if (x > W - 20 || x < kHeaderW) m_hbar->setValue(m_hbar->value() + x - kHeaderW - 40);
}

void Timeline::resizeEvent(QResizeEvent*) {
    const int vw = m_vbar->sizeHint().width(), hh = m_hbar->sizeHint().height();
    m_hbar->setGeometry(kHeaderW, height() - hh, width() - vw - kHeaderW, hh);
    m_vbar->setGeometry(width() - vw, lanesTop(), vw, height() - hh - lanesTop());
    refreshLayout();
}

void Timeline::selectTrack(int t) {
    m_selTrack = t;
    m_selRegion = -1;
    update();
    emit selectionChanged(m_selTrack, m_selRegion);
}

void Timeline::selectRegion(int t, int r) {
    m_selTrack = t;
    m_selRegion = r;
    update();
    emit selectionChanged(t, r);
}

void Timeline::setAutomationMode(bool on) {
    m_autoMode = on;
    update();
}

void Timeline::showTempoTrack(bool on) {
    m_showTempo = on;
    resizeEvent(nullptr);
}

// ---------------------------------------------------------------- editing commands
void Timeline::deleteSelected() {
    if (m_selTrack >= 0 && m_selRegion >= 0) {
        m_e->removeRegion(m_selTrack, m_selRegion);
        m_selRegion = -1;
        emit selectionChanged(m_selTrack, -1);
    }
}

void Timeline::splitAtPlayhead() {
    if (m_selTrack >= 0 && m_selRegion >= 0) m_e->splitRegion(m_selTrack, m_selRegion, m_e->positionBeats());
}

void Timeline::duplicateSelected() {
    if (m_selTrack >= 0 && m_selRegion >= 0) {
        m_selRegion = m_e->duplicateRegion(m_selTrack, m_selRegion);
        emit selectionChanged(m_selTrack, m_selRegion);
    }
}

// ---------------------------------------------------------------- painting
void Timeline::paintEvent(QPaintEvent*) {
    const auto tr = m_e->snapshot();
    const auto tm = m_e->tempoMap();
    QPainter p(this);
    const QColor text = theme::kText;
    const int W = width() - m_vbar->width(), H = height() - m_hbar->height();
    const int scrollX = m_hbar->value(), top = lanesTop();
    p.fillRect(rect(), palette().color(QPalette::Base));

    const double beatL = std::max(0.0, xToBeat(kHeaderW)), beatR = xToBeat(W);
    // Calls f(beat, bar number or 0, drawn as a bar line) for every visible bar line and beat line.
    auto forEachGrid = [&](const std::function<void(double, int)>& f) {
        for (int bar = std::max(1, tm->barAt(beatL)); bar < 100000; ++bar) {
            const double b = tm->barStart(bar);
            if (b > beatR) break;
            f(b, bar);
            const dsp::MeterPoint mp = tm->meterAtBeat(b);
            const double unit = 4.0 / mp.den, len = tm->barLength(bar);
            if (m_ppb * unit >= 10)
                for (double bb = b + unit; bb < b + len - 1e-9; bb += unit) f(bb, 0);
        }
    };

    // Lane backgrounds and grid.
    p.setClipRect(0, top, W, H - top);
    for (int i = 0; i < int(tr.size()); ++i)
        p.fillRect(kHeaderW, laneY(i), W - kHeaderW, kLaneH - 1, palette().color(i % 2 ? QPalette::AlternateBase : QPalette::Base));
    forEachGrid([&](double beat, int bar) {
        const int x = beatToX(beat);
        if (x < kHeaderW) return;
        QColor g = text;
        g.setAlpha(bar ? 66 : 24);
        p.setPen(g);
        p.drawLine(x, top, x, H);
    });

    // Regions, drum patterns and automation.
    for (int i = 0; i < int(tr.size()); ++i) {
        const int y = laneY(i);
        if (y + kLaneH < top || y > H) continue;
        const Track& t = tr[size_t(i)];
        p.setClipRect(kHeaderW, std::max(top, y), W - kHeaderW, kLaneH);

        if (t.type == TrackType::Drums) {
            QColor c = t.color;
            c.setAlpha(150);
            const int k0 = std::max(0, int(std::floor(beatL * 4))), k1 = int(std::ceil(beatR * 4));
            for (int k = k0; k <= k1; ++k) {
                int cnt = 0;
                for (int r = 0; r < kDrumRows; ++r) cnt += t.steps[size_t(r)][size_t(k % kSteps)] ? 1 : 0;
                if (!cnt) continue;
                const int x = beatToX(k * 0.25), w = std::max(2, int(m_ppb / 4.0) - 2);
                const int h = cnt * (kLaneH - 16) / kDrumRows;
                p.fillRect(x, y + kLaneH - 8 - h, w, h, c);
            }
        }

        for (size_t ri = 0; ri < t.regions.size(); ++ri) {
            const Region& rg = t.regions[ri];
            const double len = m_e->regionBeats(rg);
            const int x0 = beatToX(rg.start), x1 = beatToX(rg.start + len);
            if (x1 < kHeaderW || x0 > W) continue;
            const QRect r(x0, y + 3, std::max(3, x1 - x0), kLaneH - 7);
            const bool sel = (i == m_selTrack && int(ri) == m_selRegion);
            QColor fill = t.color;
            fill.setAlpha(sel ? 170 : 105);
            p.setPen(sel ? QPen(Qt::white, 2) : QPen(t.color.lighter(135)));
            p.setBrush(fill);
            p.drawRoundedRect(r, 4, 4);
            p.fillRect(QRect(r.left() + 1, r.top() + 1, r.width() - 2, 15), t.color.darker(sel ? 105 : 135));

            const QRect body(r.left() + 1, r.top() + 17, r.width() - 2, r.height() - 19);
            if (rg.midi && !rg.notes.empty()) {
                int lo = 127, hi = 0;
                for (const Note& n : rg.notes) { lo = std::min(lo, n.pitch); hi = std::max(hi, n.pitch); }
                const double rowH = double(body.height()) / double(hi - lo + 1);
                p.setPen(Qt::NoPen);
                p.setBrush(t.color.lighter(185));
                for (const Note& n : rg.notes) {
                    if (n.start >= rg.lengthBeats) continue;
                    p.drawRect(QRectF(beatToX(rg.start + n.start), body.top() + double(hi - n.pitch) * rowH,
                                      std::max(2.0, n.length * m_ppb - 1), std::max(2.0, rowH - 1)));
                }
            } else if (rg.audio) {
                p.setPen(t.color.lighter(170));
                const int mid = body.center().y(), amp = body.height() / 2 - 1;
                const auto& pk = rg.audio->peaks;
                const double startFrame = tm->beatToFrame(rg.start);
                const int xa = std::max(r.left() + 1, kHeaderW), xb = std::min(r.right() - 1, W);
                for (int x = xa; x < xb; ++x) {
                    const qint64 f0 = rg.offset + qint64(tm->beatToFrame(xToBeat(x)) - startFrame);
                    const qint64 f1 = rg.offset + qint64(tm->beatToFrame(xToBeat(x + 1)) - startFrame);
                    const size_t i0 = size_t(std::max<qint64>(0, f0) / kPeakStride), i1 = size_t(std::max<qint64>(0, f1) / kPeakStride);
                    float m = 0;
                    for (size_t k = i0; k <= i1 && k < pk.size(); ++k) m = std::max(m, pk[k]);
                    const int h = int(std::min(1.f, m) * amp);
                    p.drawLine(x, mid - h, x, mid + h);
                }
            }
            QString label = rg.name;
            if (rg.follow) label += QString("  ·  %1 bpm").arg(rg.sourceBpm, 0, 'f', 0);
            if (std::fabs(rg.semitones) > 0.01) label += QString("  %1%2 st").arg(rg.semitones > 0 ? "+" : "").arg(rg.semitones, 0, 'f', 0);
            p.setPen(Qt::white);
            p.drawText(QRect(r.left() + 5, r.top() + 1, r.width() - 8, 15), Qt::AlignVCenter | Qt::AlignLeft,
                       fontMetrics().elidedText(label, Qt::ElideRight, r.width() - 10));
        }

        if (m_autoMode) {   // volume automation overlay
            p.setBrush(Qt::NoBrush);
            if (t.volAuto.empty()) {
                p.setPen(QPen(theme::kYellow, 1, Qt::DashLine));
                p.drawLine(kHeaderW, volumeToY(t.volume, i), W, volumeToY(t.volume, i));
            } else {
                p.setPen(QPen(theme::kYellow, 2));
                int px = kHeaderW, py = volumeToY(t.volAuto.front().value, i);
                for (const AutoPoint& a : t.volAuto) {
                    const int ax = beatToX(a.beat), ay = volumeToY(a.value, i);
                    p.drawLine(px, py, ax, ay);
                    px = ax; py = ay;
                }
                p.drawLine(px, py, W, py);
                p.setBrush(theme::kYellow);
                for (const AutoPoint& a : t.volAuto) p.drawEllipse(QPoint(beatToX(a.beat), volumeToY(a.value, i)), 4, 4);
            }
        }
    }

    // Track headers.
    p.setClipRect(0, top, kHeaderW, H - top);
    for (int i = 0; i < int(tr.size()); ++i) {
        const int y = laneY(i);
        const Track& t = tr[size_t(i)];
        p.fillRect(0, y, kHeaderW - 1, kLaneH - 1, i == m_selTrack ? QColor(64, 64, 72) : palette().color(QPalette::Window));
        p.fillRect(0, y, 4, kLaneH - 1, t.color);
        drawTrackGlyph(p, QRect(12, y + 8, 26, 26), t.type, t.color);
        QFont bold = font();
        bold.setBold(true);
        p.setFont(bold);
        p.setPen(text);
        p.drawText(QRect(46, y + 6, kHeaderW - 54, 18), Qt::AlignLeft | Qt::AlignVCenter,
                   QFontMetrics(bold).elidedText(t.name, Qt::ElideRight, kHeaderW - 54));
        p.setFont(font());
        p.setPen(theme::kDim);
        const char* kind = t.type == TrackType::Audio ? "Audio" : t.type == TrackType::Instrument ? "Software Instrument"
                         : t.type == TrackType::Drums ? "Drum Machine" : "Drummer";
        p.drawText(QRect(46, y + 24, kHeaderW - 54, 14), Qt::AlignLeft, QObject::tr(kind));
        auto btn = [&](const QRect& r, const QString& s, bool on, const QColor& c) {
            p.setPen(on ? Qt::black : text);
            p.setBrush(on ? c : QColor(70, 70, 76));
            p.drawRoundedRect(r, 3, 3);
            p.drawText(r, Qt::AlignCenter, s);
        };
        btn(muteRect(i), "M", t.mute, theme::kOrange);
        btn(soloRect(i), "S", t.solo, theme::kYellow);
        if (t.type != TrackType::Drums) btn(armRect(i), "R", t.armed, theme::kRed);
        // A thin volume indicator under the buttons, like the mini fader in Logic's header.
        const QRect vr(14, y + kLaneH - 12, kHeaderW - 28, 4);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(20, 20, 22));
        p.drawRoundedRect(vr, 2, 2);
        p.setBrush(t.color.lighter(120));
        p.drawRoundedRect(QRect(vr.left(), vr.top(), int(vr.width() * std::min(1.f, t.volume / 1.5f)), vr.height()), 2, 2);
    }

    // Tempo track.
    if (m_showTempo) {
        p.setClipRect(0, kRulerH, W, kTempoH);
        p.fillRect(kHeaderW, kRulerH, W - kHeaderW, kTempoH, QColor(36, 36, 40));
        p.fillRect(0, kRulerH, kHeaderW - 1, kTempoH, palette().color(QPalette::Window));
        p.setPen(text);
        QFont bold = font();
        bold.setBold(true);
        p.setFont(bold);
        p.drawText(QRect(14, kRulerH + 6, kHeaderW - 20, 18), Qt::AlignLeft, QObject::tr("Tempo"));
        p.setFont(font());
        p.setPen(theme::kDim);
        p.drawText(QRect(14, kRulerH + 26, kHeaderW - 20, 30), Qt::AlignLeft | Qt::TextWordWrap, QObject::tr("Click to add a point, drag to change it."));
        p.setClipRect(kHeaderW, kRulerH, W - kHeaderW, kTempoH);
        for (int bpm : {60, 120, 180, 240}) {
            p.setPen(QColor(255, 255, 255, 22));
            p.drawLine(kHeaderW, bpmToY(bpm), W, bpmToY(bpm));
            p.setPen(theme::kDim);
            p.drawText(kHeaderW + 3, bpmToY(bpm) - 2, QString::number(bpm));
        }
        p.setPen(QPen(theme::kAccent, 2));
        QPoint prev;
        for (int x = kHeaderW; x < W; x += 2) {
            const QPoint cur(x, bpmToY(tm->bpmAt(xToBeat(x))));
            if (x > kHeaderW) p.drawLine(prev, cur);
            prev = cur;
        }
        p.setBrush(theme::kAccent);
        p.setPen(Qt::white);
        for (const dsp::TempoPoint& tp : tm->tempoPoints()) {
            const QPoint c(beatToX(tp.beat), bpmToY(tp.bpm));
            p.drawEllipse(c, 5, 5);
            p.drawText(c.x() + 7, c.y() - 7, QString::number(tp.bpm, 'f', 1));
        }
    }

    // Ruler: bars, beats, time signatures and the cycle range.
    p.setClipping(false);
    p.fillRect(0, 0, W, kRulerH, palette().color(QPalette::Window));
    if (m_e->loopOn()) {
        const int la = std::max(kHeaderW, beatToX(m_e->loopA())), lb = std::min(W, beatToX(m_e->loopB()));
        if (lb > la) p.fillRect(la, 2, lb - la, kRulerH - 4, QColor(250, 200, 60, 110));
    }
    p.setClipRect(kHeaderW, 0, W - kHeaderW, kRulerH);
    const int barStep = std::max(1, int(std::ceil(46.0 / std::max(1.0, tm->barLength(1) * m_ppb))));   // label every n-th bar when zoomed out
    forEachGrid([&](double beat, int bar) {
        const int x = beatToX(beat);
        p.setPen(text);
        p.drawLine(x, bar ? 6 : kRulerH - 9, x, kRulerH);
        if (bar && (bar - 1) % barStep == 0) p.drawText(x + 4, 17, QString::number(bar));
    });
    for (const dsp::MeterPoint& mp : tm->meterPoints()) {   // time signature markers
        const int x = beatToX(tm->barStart(mp.bar));
        p.setPen(Qt::NoPen);
        p.setBrush(theme::kAccent);
        p.drawRoundedRect(QRect(x + 1, kRulerH - 15, 28, 13), 3, 3);
        p.setPen(Qt::white);
        p.drawText(QRect(x + 1, kRulerH - 15, 28, 13), Qt::AlignCenter, QString("%1/%2").arg(mp.num).arg(mp.den));
    }

    // Playhead.
    p.setClipRect(kHeaderW, 0, W - kHeaderW, H);
    const int hx = beatToX(m_e->positionBeats());
    p.setPen(QPen(Qt::white, 1));
    p.drawLine(hx, 0, hx, H);
    p.setBrush(Qt::white);
    p.drawPolygon(QPolygon({QPoint(hx - 5, 0), QPoint(hx + 5, 0), QPoint(hx, 9)}));
    p.setClipping(false);
    p.fillRect(0, 0, kHeaderW, kRulerH, palette().color(QPalette::Window));
    p.setPen(theme::kDim);
    p.drawText(QRect(14, 0, kHeaderW - 20, kRulerH), Qt::AlignVCenter | Qt::AlignLeft, QObject::tr("Tracks"));
}

// ---------------------------------------------------------------- mouse
void Timeline::tempoPress(QMouseEvent* e) {
    const QPoint pt = e->position().toPoint();
    auto pts = m_e->tempoMap()->tempoPoints();
    const int hit = tempoPointAt(pts, pt);
    if (e->button() == Qt::RightButton) {
        if (hit < 0) return;
        QMenu menu(this);
        QAction* ramp = menu.addAction(Timeline::tr("Ramp to Next Point"));
        ramp->setCheckable(true);
        ramp->setChecked(pts[size_t(hit)].ramp);
        QAction* del = menu.addAction(Timeline::tr("Delete Point"));
        del->setEnabled(hit > 0);
        QAction* a = menu.exec(e->globalPosition().toPoint());
        if (a == ramp) { m_e->checkpoint(); pts[size_t(hit)].ramp = !pts[size_t(hit)].ramp; m_e->setTempoPoints(pts, true); }
        else if (a == del) { m_e->checkpoint(); pts.erase(pts.begin() + hit); m_e->setTempoPoints(pts, true); }
        return;
    }
    m_e->checkpoint();
    if (hit < 0) {
        pts.push_back({snapBeat(xToBeat(pt.x())), yToBpm(pt.y()), false});
        std::sort(pts.begin(), pts.end(), [](const dsp::TempoPoint& a, const dsp::TempoPoint& b) { return a.beat < b.beat; });
        for (size_t i = 0; i < pts.size(); ++i)
            if (std::fabs(pts[i].beat - snapBeat(xToBeat(pt.x()))) < 1e-9) m_tempoIdx = int(i);
        m_e->setTempoPoints(pts, false);
    } else {
        m_tempoIdx = hit;
    }
    m_drag = Drag::TempoPoint;
}

void Timeline::mousePressEvent(QMouseEvent* e) {
    setFocus();
    const QPoint pt = e->position().toPoint();
    if (pt.x() > width() - m_vbar->width()) return;

    // Ruler: left drag moves the playhead, right drag defines the cycle range.
    if (pt.y() < kRulerH && pt.x() >= kHeaderW) {
        if (e->button() == Qt::LeftButton) {
            m_drag = Drag::Playhead;
            m_e->seekBeats(m_snap ? snapBeat(xToBeat(pt.x())) : xToBeat(pt.x()));
        } else if (e->button() == Qt::RightButton) {
            m_drag = Drag::Loop;
            m_loopAnchor = std::round(xToBeat(pt.x()));
        }
        return;
    }
    if (m_showTempo && pt.y() < lanesTop()) {
        if (pt.x() >= kHeaderW) tempoPress(e);
        return;
    }
    const int ti = laneAt(pt.y());
    const auto trk = m_e->snapshot();
    if (ti < 0 || ti >= int(trk.size())) return;
    const Track& t = trk[size_t(ti)];

    // Track header.
    if (pt.x() < kHeaderW) {
        m_selTrack = ti;
        m_selRegion = -1;
        if (e->button() == Qt::RightButton) {
            QMenu menu(this);
            QAction* ren = menu.addAction(Timeline::tr("Rename Track…"));
            QAction* del = menu.addAction(Timeline::tr("Delete Track"));
            QAction* a = menu.exec(e->globalPosition().toPoint());
            if (a == ren) {
                bool ok = false;
                const QString n = QInputDialog::getText(this, Timeline::tr("Rename Track"), Timeline::tr("Track name:"), QLineEdit::Normal, t.name, &ok);
                if (ok && !n.isEmpty()) m_e->renameTrack(ti, n);
            } else if (a == del) {
                m_selTrack = -1;
                m_e->removeTrack(ti);
            }
        } else if (muteRect(ti).contains(pt)) m_e->setMute(ti, !t.mute);
        else if (soloRect(ti).contains(pt)) m_e->setSolo(ti, !t.solo);
        else if (t.type != TrackType::Drums && armRect(ti).contains(pt)) m_e->setArmed(ti, !t.armed);
        update();
        emit selectionChanged(m_selTrack, -1);
        return;
    }

    // Automation mode: click adds or drags a breakpoint, right click removes it.
    if (m_autoMode) {
        std::vector<AutoPoint> pts = t.volAuto;
        int hit = -1;
        for (size_t k = 0; k < pts.size(); ++k)
            if (std::abs(beatToX(pts[k].beat) - pt.x()) <= 6 && std::abs(volumeToY(pts[k].value, ti) - pt.y()) <= 6) hit = int(k);
        m_selTrack = ti;
        m_e->checkpoint();
        if (e->button() == Qt::RightButton) {
            if (hit >= 0) { pts.erase(pts.begin() + hit); m_e->setVolAuto(ti, pts); }
            return;
        }
        if (hit < 0) {
            pts.push_back({snapBeat(xToBeat(pt.x())), yToVolume(pt.y(), ti)});
            std::sort(pts.begin(), pts.end(), [](const AutoPoint& a, const AutoPoint& b) { return a.beat < b.beat; });
            for (size_t k = 0; k < pts.size(); ++k)
                if (std::abs(pts[k].beat - snapBeat(xToBeat(pt.x()))) < 1e-9) hit = int(k);
            m_e->setVolAuto(ti, pts);
        }
        m_autoIdx = hit;
        m_drag = Drag::AutoPoint;
        update();
        return;
    }

    // Regions.
    int rt, rr;
    bool edge;
    const bool onRegion = hitRegion(trk, pt, rt, rr, edge);
    const bool changedSel = (ti != m_selTrack);
    m_selTrack = ti;
    m_selRegion = onRegion ? rr : -1;
    if (onRegion && e->button() == Qt::LeftButton) {
        m_e->checkpoint();
        m_drag = edge ? Drag::Resize : Drag::Move;
        m_dragOff = xToBeat(pt.x()) - trk[size_t(ti)].regions[size_t(rr)].start;
    }
    update();
    if (changedSel || onRegion || m_selRegion < 0) emit selectionChanged(m_selTrack, m_selRegion);
}

void Timeline::mouseMoveEvent(QMouseEvent* e) {
    const QPoint pt = e->position().toPoint();
    switch (m_drag) {
    case Drag::Playhead: m_e->seekBeats(m_snap ? snapBeat(xToBeat(pt.x())) : std::max(0.0, xToBeat(pt.x()))); break;
    case Drag::Move: m_e->moveRegion(m_selTrack, m_selRegion, snapBeat(xToBeat(pt.x()) - m_dragOff)); break;
    case Drag::Resize: {
        const auto trk = m_e->snapshot();
        if (m_selTrack >= 0 && m_selTrack < int(trk.size()) && m_selRegion >= 0) {
            const Region& rg = trk[size_t(m_selTrack)].regions[size_t(m_selRegion)];
            m_e->resizeRegion(m_selTrack, m_selRegion, std::max(0.25, snapBeat(xToBeat(pt.x())) - rg.start));
        }
        break;
    }
    case Drag::Loop: {
        const double cur = std::max(0.0, std::round(xToBeat(pt.x())));
        const double a = std::min(m_loopAnchor, cur), b = std::max(m_loopAnchor, cur);
        if (b - a >= 1.0) m_e->setLoop(true, a, b);
        break;
    }
    case Drag::AutoPoint: {
        auto pts = m_e->snapshot()[size_t(m_selTrack)].volAuto;
        if (m_autoIdx >= 0 && m_autoIdx < int(pts.size())) {
            pts[size_t(m_autoIdx)].beat = snapBeat(xToBeat(pt.x()));
            pts[size_t(m_autoIdx)].value = yToVolume(pt.y(), m_selTrack);
            m_e->setVolAuto(m_selTrack, pts);
        }
        break;
    }
    case Drag::TempoPoint: {
        auto pts = m_e->tempoMap()->tempoPoints();
        if (m_tempoIdx >= 0 && m_tempoIdx < int(pts.size())) {
            if (m_tempoIdx > 0) pts[size_t(m_tempoIdx)].beat = snapBeat(xToBeat(pt.x()));
            pts[size_t(m_tempoIdx)].bpm = yToBpm(pt.y());
            m_e->setTempoPoints(pts, false);   // regions are re-stretched when the mouse is released
        }
        break;
    }
    case Drag::None: {   // hover: show a resize cursor over region edges
        const auto trk = m_e->snapshot();
        int t, r;
        bool edge = false;
        setCursor(!m_autoMode && hitRegion(trk, pt, t, r, edge) && edge ? Qt::SizeHorCursor : Qt::ArrowCursor);
        break;
    }
    }
}

void Timeline::mouseReleaseEvent(QMouseEvent*) {
    if (m_drag == Drag::TempoPoint) m_e->commitTempo();
    m_drag = Drag::None;
}

void Timeline::mouseDoubleClickEvent(QMouseEvent* e) {
    const QPoint pt = e->position().toPoint();
    const auto trk = m_e->snapshot();
    int t, r;
    bool edge;
    if (hitRegion(trk, pt, t, r, edge)) {
        if (trk[size_t(t)].regions[size_t(r)].midi) emit regionOpened(t, r);
        return;
    }
    // Double-click on an empty part of an instrument or Drummer lane creates a region of one bar / four bars.
    const int ti = laneAt(pt.y());
    if (m_autoMode || ti < 0 || ti >= int(trk.size()) || pt.x() < kHeaderW) return;
    const auto tm = m_e->tempoMap();
    const double beat = xToBeat(pt.x());
    const double start = tm->barStart(tm->barAt(beat));
    int idx = -1;
    if (trk[size_t(ti)].type == TrackType::Instrument) idx = m_e->addMidiRegion(ti, start, tm->barLength(tm->barAt(beat)));
    else if (trk[size_t(ti)].type == TrackType::Drummer) idx = m_e->addDrummerRegion(ti, start, 4);
    if (idx >= 0) {
        selectRegion(ti, idx);
        emit regionOpened(ti, idx);
    }
}

void Timeline::wheelEvent(QWheelEvent* e) {
    const int dy = e->angleDelta().y(), dx = e->angleDelta().x();
    if (e->modifiers() & Qt::ControlModifier) {   // zoom around the cursor
        const int x = int(e->position().x());
        const double beatAtCursor = xToBeat(x);
        m_ppb = std::clamp(m_ppb * (dy > 0 ? 1.2 : 1 / 1.2), 6.0, 400.0);
        refreshLayout();
        m_hbar->setValue(int(beatAtCursor * m_ppb) - (x - kHeaderW));
    } else if ((e->modifiers() & Qt::ShiftModifier) || dx != 0) {
        m_hbar->setValue(m_hbar->value() - (dx ? dx : dy) / 2);
    } else {
        m_vbar->setValue(m_vbar->value() - dy / 2);
    }
    e->accept();
}

void Timeline::dragEnterEvent(QDragEnterEvent* e) {
    if (e->mimeData()->hasUrls()) e->acceptProposedAction();
}

void Timeline::dropEvent(QDropEvent* e) {
    const int ti = laneAt(int(e->position().y()));
    const double beat = snapBeat(xToBeat(int(e->position().x())));
    for (const QUrl& u : e->mimeData()->urls()) {
        const QString path = u.toLocalFile();
        if (path.isEmpty()) continue;
        const QString ext = QFileInfo(path).suffix().toLower();
        if (ext == "mid" || ext == "midi") {
            QString err;
            if (!m_e->importMidiFile(path, &err)) emit m_e->message(err);
        } else {
            m_e->importAudio(path, ti, beat);
        }
    }
    e->acceptProposedAction();
}
