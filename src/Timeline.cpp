// MiniDAW - a lightweight Logic Pro-inspired digital audio workstation
// Copyright (C) 2026 MiniDAW contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Timeline.h"

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QInputDialog>
#include <QLineEdit>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QUrl>
#include <QWheelEvent>
#include <cmath>
#include "Theme.h"

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
    if (y < kRulerH) return -1;
    return (y - kRulerH + m_vbar->value()) / kLaneH;
}

QRect Timeline::muteRect(int i) const { return QRect(12, laneY(i) + kLaneH - 28, 24, 18); }
QRect Timeline::soloRect(int i) const { return QRect(40, laneY(i) + kLaneH - 28, 24, 18); }
QRect Timeline::armRect(int i) const { return QRect(68, laneY(i) + kLaneH - 28, 24, 18); }

float Timeline::yToVolume(int y, int lane) const {
    const int top = laneY(lane) + 6, bottom = laneY(lane) + kLaneH - 6;
    return std::clamp(1.5f * (1.f - float(y - top) / float(bottom - top)), 0.f, 1.5f);
}

int Timeline::volumeToY(float v, int lane) const {
    const int top = laneY(lane) + 6, bottom = laneY(lane) + kLaneH - 6;
    return bottom - int(v / 1.5f * float(bottom - top));
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
    m_vbar->setRange(0, std::max(0, kRulerH + m_e->trackCount() * kLaneH - H));
    m_vbar->setPageStep(std::max(1, H - kRulerH));
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
    m_vbar->setGeometry(width() - vw, kRulerH, vw, height() - hh - kRulerH);
    refreshLayout();
}

void Timeline::selectTrack(int t) {
    m_selTrack = t;
    m_selRegion = -1;
    update();
    emit selectionChanged(m_selTrack, m_selRegion);
}

void Timeline::setAutomationMode(bool on) {
    m_autoMode = on;
    update();
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
    QPainter p(this);
    const QColor text = theme::kText;
    const int W = width() - m_vbar->width(), H = height() - m_hbar->height();
    const int scrollX = m_hbar->value();
    p.fillRect(rect(), palette().color(QPalette::Base));

    // Lane backgrounds and beat grid.
    p.setClipRect(0, kRulerH, W, H - kRulerH);
    for (int i = 0; i < int(tr.size()); ++i)
        p.fillRect(kHeaderW, laneY(i), W - kHeaderW, kLaneH - 1, palette().color(i % 2 ? QPalette::AlternateBase : QPalette::Base));
    const int b0 = std::max(0, int(std::floor(scrollX / m_ppb))), b1 = int((scrollX + W) / m_ppb) + 1;
    for (int b = b0; b <= b1; ++b) {
        const bool bar = b % kBeatsPerBar == 0;
        if (!bar && m_ppb < 10) continue;
        const int x = beatToX(b);
        if (x < kHeaderW) continue;
        QColor g = text;
        g.setAlpha(bar ? 70 : 26);
        p.setPen(g);
        p.drawLine(x, kRulerH, x, H);
    }

    // Regions, drum patterns and automation.
    const double fpp = m_e->framesPerBeat() / m_ppb;   // audio frames per pixel
    for (int i = 0; i < int(tr.size()); ++i) {
        const int y = laneY(i);
        if (y + kLaneH < kRulerH || y > H) continue;
        const Track& t = tr[size_t(i)];
        p.setClipRect(kHeaderW, std::max(kRulerH, y), W - kHeaderW, kLaneH);

        if (t.type == TrackType::Drums) {
            const double stepPx = m_ppb / 4.0;
            const int k0 = std::max(0, int(std::floor(scrollX / stepPx))), k1 = int((scrollX + W) / stepPx) + 1;
            QColor c = t.color;
            c.setAlpha(150);
            for (int k = k0; k <= k1; ++k) {
                int cnt = 0;
                for (int r = 0; r < kDrumRows; ++r) cnt += t.steps[size_t(r)][size_t(k % kSteps)] ? 1 : 0;
                if (!cnt) continue;
                const int h = cnt * (kLaneH - 16) / kDrumRows;
                p.fillRect(kHeaderW + int(k * stepPx) - scrollX, y + kLaneH - 8 - h, std::max(2, int(stepPx) - 2), h, c);
            }
        }

        for (size_t ri = 0; ri < t.regions.size(); ++ri) {
            const Region& rg = t.regions[ri];
            const int x0 = beatToX(rg.start), x1 = beatToX(rg.start + m_e->regionBeats(rg));
            if (x1 < kHeaderW || x0 > W) continue;
            const QRect r(x0, y + 3, std::max(3, x1 - x0), kLaneH - 7);
            const bool sel = (i == m_selTrack && int(ri) == m_selRegion);
            QColor fill = t.color;
            fill.setAlpha(sel ? 150 : 95);
            p.setPen(sel ? QPen(Qt::white, 2) : QPen(t.color.lighter(130)));
            p.setBrush(fill);
            p.drawRoundedRect(r, 4, 4);
            p.fillRect(QRect(r.left() + 1, r.top() + 1, r.width() - 2, 14), t.color.darker(sel ? 110 : 140));

            const QRect body(r.left() + 1, r.top() + 16, r.width() - 2, r.height() - 18);
            if (rg.midi && !rg.notes.empty()) {
                int lo = 127, hi = 0;
                for (const Note& n : rg.notes) { lo = std::min(lo, n.pitch); hi = std::max(hi, n.pitch); }
                const double rowH = double(body.height()) / double(hi - lo + 1);
                p.setPen(Qt::NoPen);
                p.setBrush(t.color.lighter(170));
                for (const Note& n : rg.notes) {
                    const int nx = beatToX(rg.start + n.start);
                    p.drawRect(QRectF(nx, body.top() + double(hi - n.pitch) * rowH, std::max(2.0, n.length * m_ppb - 1), std::max(2.0, rowH - 1)));
                }
            } else if (rg.audio) {
                p.setPen(t.color.lighter(160));
                const int mid = body.center().y(), amp = body.height() / 2 - 1;
                const auto& pk = rg.audio->peaks;
                const int xa = std::max(r.left() + 1, kHeaderW), xb = std::min(r.right() - 1, W);
                for (int x = xa; x < xb; ++x) {
                    const qint64 f0 = rg.offset + qint64((x - x0) * fpp);
                    const size_t i0 = size_t(f0 / kPeakStride), i1 = size_t((f0 + qint64(fpp)) / kPeakStride);
                    float m = 0;
                    for (size_t k = i0; k <= i1 && k < pk.size(); ++k) m = std::max(m, pk[k]);
                    const int h = int(std::min(1.f, m) * amp);
                    p.drawLine(x, mid - h, x, mid + h);
                }
            }
            p.setPen(Qt::white);
            p.drawText(QRect(r.left() + 5, r.top() + 1, r.width() - 8, 14), Qt::AlignVCenter | Qt::AlignLeft,
                       fontMetrics().elidedText(rg.name, Qt::ElideRight, r.width() - 10));
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
    p.setClipRect(0, kRulerH, kHeaderW, H - kRulerH);
    for (int i = 0; i < int(tr.size()); ++i) {
        const int y = laneY(i);
        const Track& t = tr[size_t(i)];
        p.fillRect(0, y, kHeaderW - 1, kLaneH - 1, i == m_selTrack ? QColor(62, 62, 68) : palette().color(QPalette::Window));
        p.fillRect(0, y, 5, kLaneH - 1, t.color);
        QFont bold = font();
        bold.setBold(true);
        p.setFont(bold);
        p.setPen(text);
        p.drawText(QRect(14, y + 6, kHeaderW - 22, 18), Qt::AlignLeft | Qt::AlignVCenter,
                   QFontMetrics(bold).elidedText(t.name, Qt::ElideRight, kHeaderW - 22));
        p.setFont(font());
        p.setPen(theme::kDim);
        p.drawText(QRect(14, y + 24, kHeaderW - 22, 14), Qt::AlignLeft,
                   t.type == TrackType::Audio ? "Audio" : t.type == TrackType::Instrument ? "Software Instrument" : "Drum Machine");
        auto btn = [&](const QRect& r, const QString& s, bool on, const QColor& c) {
            p.setPen(on ? Qt::black : text);
            p.setBrush(on ? c : QColor(70, 70, 76));
            p.drawRoundedRect(r, 3, 3);
            p.drawText(r, Qt::AlignCenter, s);
        };
        btn(muteRect(i), "M", t.mute, theme::kOrange);
        btn(soloRect(i), "S", t.solo, theme::kYellow);
        if (t.type != TrackType::Drums) btn(armRect(i), "R", t.armed, theme::kRed);
    }

    // Ruler with cycle range.
    p.setClipping(false);
    p.fillRect(0, 0, W, kRulerH, palette().color(QPalette::Window));
    if (m_e->loopOn()) {
        const int la = std::max(kHeaderW, beatToX(m_e->loopA())), lb = std::min(W, beatToX(m_e->loopB()));
        if (lb > la) p.fillRect(la, 2, lb - la, kRulerH - 4, QColor(250, 200, 60, 110));
    }
    p.setClipRect(kHeaderW, 0, W - kHeaderW, kRulerH);
    const int barStep = (m_ppb * kBeatsPerBar < 30) ? 4 : 1;
    for (int b = b0; b <= b1; ++b) {
        const bool bar = b % kBeatsPerBar == 0;
        if (!bar && m_ppb < 10) continue;
        const int x = beatToX(b);
        p.setPen(text);
        p.drawLine(x, bar ? 4 : kRulerH - 8, x, kRulerH);
        if (bar && (b / kBeatsPerBar) % barStep == 0) p.drawText(x + 4, 16, QString::number(b / kBeatsPerBar + 1));
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
}

// ---------------------------------------------------------------- mouse
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
    const int ti = laneAt(pt.y());
    const auto tr = m_e->snapshot();
    if (ti < 0 || ti >= int(tr.size())) return;
    const Track& t = tr[size_t(ti)];

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
    const bool onRegion = hitRegion(tr, pt, rt, rr, edge);
    const bool changedSel = (ti != m_selTrack);
    m_selTrack = ti;
    m_selRegion = onRegion ? rr : -1;
    if (onRegion && e->button() == Qt::LeftButton) {
        m_e->checkpoint();
        m_drag = edge ? Drag::Resize : Drag::Move;
        m_dragOff = xToBeat(pt.x()) - tr[size_t(ti)].regions[size_t(rr)].start;
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
        const auto tr = m_e->snapshot();
        if (m_selTrack >= 0 && m_selTrack < int(tr.size()) && m_selRegion >= 0) {
            const Region& rg = tr[size_t(m_selTrack)].regions[size_t(m_selRegion)];
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
    case Drag::None: {   // hover: show a resize cursor over region edges
        const auto tr = m_e->snapshot();
        int t, r;
        bool edge = false;
        setCursor(!m_autoMode && hitRegion(tr, pt, t, r, edge) && edge ? Qt::SizeHorCursor : Qt::ArrowCursor);
        break;
    }
    }
}

void Timeline::mouseReleaseEvent(QMouseEvent*) { m_drag = Drag::None; }

void Timeline::mouseDoubleClickEvent(QMouseEvent* e) {
    const QPoint pt = e->position().toPoint();
    const auto tr = m_e->snapshot();
    int t, r;
    bool edge;
    if (hitRegion(tr, pt, t, r, edge)) {
        if (tr[size_t(t)].regions[size_t(r)].midi) emit regionOpened(t, r);
        return;
    }
    // Double-click on an empty part of an instrument lane creates a one-bar MIDI region.
    const int ti = laneAt(pt.y());
    if (!m_autoMode && ti >= 0 && ti < int(tr.size()) && pt.x() >= kHeaderW && tr[size_t(ti)].type == TrackType::Instrument) {
        const double start = std::floor(xToBeat(pt.x()) / kBeatsPerBar) * kBeatsPerBar;
        const int idx = m_e->addMidiRegion(ti, start, kBeatsPerBar);
        if (idx >= 0) {
            m_selTrack = ti;
            m_selRegion = idx;
            emit selectionChanged(ti, idx);
            emit regionOpened(ti, idx);
        }
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
        if (!path.isEmpty()) m_e->importAudio(path, ti, beat);
    }
    e->acceptProposedAction();
}
