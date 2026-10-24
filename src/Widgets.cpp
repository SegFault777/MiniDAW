#include "Widgets.h"

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSlider>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <cmath>

// =================================================================== Meter
Meter::Meter(QWidget* p) : QWidget(p) { setFixedWidth(14); setMinimumHeight(60); }

void Meter::setLevel(float l, float r) {
    m_l = std::max(l, m_l * 0.85f);
    m_r = std::max(r, m_r * 0.85f);
    update();
}

void Meter::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), QColor(20, 20, 22));
    auto bar = [&](int x, int w, float v) {
        const float db = 20.f * std::log10(std::max(v, 1e-4f));
        const float fr = std::clamp((db + 60.f) / 60.f, 0.f, 1.f);
        const int h = int(fr * height());
        const QColor c = fr > 0.97f ? QColor(255, 60, 60) : fr > 0.8f ? QColor(255, 200, 50) : QColor(70, 210, 110);
        p.fillRect(x, height() - h, w, h, c);
    };
    bar(1, width() / 2 - 1, m_l);
    bar(width() / 2 + 1, width() / 2 - 1, m_r);
}

// ========================================================== StepSequencer
static const char* kRowNames[kDrumRows] = {"킥", "스네어", "하이햇", "클랩"};

StepSequencer::StepSequencer(Engine* e, QWidget* p) : QWidget(p), m_e(e) { setMinimumHeight(150); }

QRectF StepSequencer::cell(int r, int s) const {
    const double lw = 70, cw = (width() - lw - 8) / kSteps, ch = (height() - 8) / double(kDrumRows);
    return QRectF(lw + s * cw, 4 + r * ch, cw - 3, ch - 3);
}

bool StepSequencer::hit(const QPointF& pt, int& r, int& s) const {
    for (r = 0; r < kDrumRows; ++r)
        for (s = 0; s < kSteps; ++s)
            if (cell(r, s).contains(pt)) return true;
    return false;
}

void StepSequencer::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const DrumPattern pat = m_e->drumPattern();
    const int cur = m_e->isPlaying() ? int(m_e->position() / (m_e->framesPerBeat() / 4.0)) % kSteps : -1;
    const QColor accent(255, 159, 28);
    for (int r = 0; r < kDrumRows; ++r) {
        p.setPen(palette().color(QPalette::Text));
        p.drawText(QRectF(6, cell(r, 0).top(), 62, cell(r, 0).height()), Qt::AlignVCenter, kRowNames[r]);
        for (int s = 0; s < kSteps; ++s) {
            const QRectF c = cell(r, s);
            const bool on = pat[size_t(r)][size_t(s)];
            QColor fill = ((s / 4) % 2) ? palette().color(QPalette::AlternateBase) : palette().color(QPalette::Button);
            if (on) fill = (s % 4 == 0) ? accent.lighter(115) : accent;
            p.setPen(s == cur ? QPen(Qt::white, 2) : Qt::NoPen);
            p.setBrush(fill);
            p.drawRoundedRect(c, 4, 4);
        }
    }
}

void StepSequencer::mousePressEvent(QMouseEvent* e) {
    int r, s;
    if (e->button() == Qt::LeftButton && hit(e->position(), r, s)) {
        m_val = !m_e->drumPattern()[size_t(r)][size_t(s)];
        m_e->setStep(r, s, m_val);
        m_drag = true; m_lastR = r; m_lastS = s;
        update();
    }
}

void StepSequencer::mouseMoveEvent(QMouseEvent* e) {
    int r, s;
    if (m_drag && hit(e->position(), r, s) && (r != m_lastR || s != m_lastS)) {
        m_e->setStep(r, s, m_val);
        m_lastR = r; m_lastS = s;
        update();
    }
}

void StepSequencer::mouseReleaseEvent(QMouseEvent*) { m_drag = false; }

// =============================================================== MixerPanel
MixerPanel::MixerPanel(Engine* e, QWidget* p) : QWidget(p), m_e(e) {
    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(4, 4, 4, 4);
    auto* sa = new QScrollArea;
    sa->setWidgetResizable(true);
    sa->setFrameShape(QFrame::NoFrame);
    m_host = new QWidget;
    m_lay = new QHBoxLayout(m_host);
    m_lay->setAlignment(Qt::AlignLeft);
    sa->setWidget(m_host);
    root->addWidget(sa, 1);

    auto* mw = new QFrame;
    mw->setFrameShape(QFrame::StyledPanel);
    mw->setFixedWidth(92);
    auto* v = new QVBoxLayout(mw);
    auto* lbl = new QLabel("마스터");
    lbl->setAlignment(Qt::AlignCenter);
    v->addWidget(lbl);
    auto* h = new QHBoxLayout;
    m_masterVol = new QSlider(Qt::Vertical);
    m_masterVol->setRange(0, 120);
    m_masterVol->setValue(90);
    m_masterMeter = new Meter;
    h->addWidget(m_masterVol);
    h->addWidget(m_masterMeter);
    v->addLayout(h, 1);
    root->addWidget(mw);
    connect(m_masterVol, &QSlider::valueChanged, this, [this](int val) { m_e->setMasterVolume(val / 100.f); });
}

void MixerPanel::rebuild() {
    for (auto& s : m_strips) { m_lay->removeWidget(s.w); s.w->hide(); s.w->deleteLater(); }
    m_strips.clear();
    const auto tr = m_e->snapshot();
    for (int i = 0; i < int(tr.size()); ++i) {
        auto* w = new QFrame;
        w->setFrameShape(QFrame::StyledPanel);
        w->setFixedWidth(92);
        auto* v = new QVBoxLayout(w);
        v->setContentsMargins(6, 6, 6, 6);
        v->setSpacing(4);
        auto* name = new QLabel(tr[size_t(i)].name);
        name->setAlignment(Qt::AlignCenter);
        name->setStyleSheet(QString("color:%1;font-weight:bold").arg(tr[size_t(i)].color.name()));
        v->addWidget(name);
        auto* h = new QHBoxLayout;
        auto* vol = new QSlider(Qt::Vertical);
        vol->setRange(0, 150);
        vol->setValue(int(std::lround(tr[size_t(i)].volume * 100)));
        auto* meter = new Meter;
        h->addWidget(vol);
        h->addWidget(meter);
        v->addLayout(h, 1);
        auto* pan = new QSlider(Qt::Horizontal);
        pan->setRange(-100, 100);
        pan->setValue(int(std::lround(tr[size_t(i)].pan * 100)));
        pan->setToolTip("패닝 (왼쪽 ↔ 오른쪽)");
        v->addWidget(pan);
        auto* bh = new QHBoxLayout;
        auto* m = new QToolButton; m->setText("M"); m->setCheckable(true); m->setToolTip("음소거");
        auto* s = new QToolButton; s->setText("S"); s->setCheckable(true); s->setToolTip("솔로");
        bh->addWidget(m); bh->addWidget(s);
        v->addLayout(bh);
        m_lay->addWidget(w);
        m_strips.push_back({w, name, vol, pan, m, s, meter});

        connect(vol, &QSlider::valueChanged, this, [this, i](int val) { m_e->setVolume(i, val / 100.f); });
        connect(pan, &QSlider::valueChanged, this, [this, i](int val) { m_e->setPan(i, val / 100.f); });
        connect(m, &QToolButton::toggled, this, [this, i](bool on) { m_e->setMute(i, on); });
        connect(s, &QToolButton::toggled, this, [this, i](bool on) { m_e->setSolo(i, on); });
    }
}

void MixerPanel::tick() {
    const auto tr = m_e->snapshot();
    if (tr.size() != m_strips.size()) { rebuild(); return; }
    for (size_t i = 0; i < tr.size(); ++i) {
        Strip& s = m_strips[i];
        const Track& t = tr[i];
        s.meter->setLevel(t.peakL, t.peakR);
        if (s.name->text() != t.name) s.name->setText(t.name);
        const int vv = int(std::lround(t.volume * 100)), pp = int(std::lround(t.pan * 100));
        if (!s.vol->isSliderDown() && s.vol->value() != vv) { QSignalBlocker b(s.vol); s.vol->setValue(vv); }
        if (!s.pan->isSliderDown() && s.pan->value() != pp) { QSignalBlocker b(s.pan); s.pan->setValue(pp); }
        if (s.m->isChecked() != t.mute) { QSignalBlocker b(s.m); s.m->setChecked(t.mute); }
        if (s.s->isChecked() != t.solo) { QSignalBlocker b(s.s); s.s->setChecked(t.solo); }
    }
    float l, r;
    m_e->masterPeaks(l, r);
    m_masterMeter->setLevel(l, r);
}

// ================================================================ Timeline
Timeline::Timeline(Engine* e, QWidget* p) : QWidget(p), m_e(e) {
    setFocusPolicy(Qt::StrongFocus);
    setAcceptDrops(true);
    setMinimumHeight(220);
    m_hbar = new QScrollBar(Qt::Horizontal, this);
    m_vbar = new QScrollBar(Qt::Vertical, this);
    connect(m_hbar, &QScrollBar::valueChanged, this, [this] { update(); });
    connect(m_vbar, &QScrollBar::valueChanged, this, [this] { update(); });
}

int Timeline::frameToX(qint64 f) const { return kHeaderW + int(std::lround(f * pxPerFrame())) - m_hbar->value(); }
qint64 Timeline::xToFrame(int x) const { return qint64(std::llround((x - kHeaderW + m_hbar->value()) / pxPerFrame())); }
int Timeline::laneY(int i) const { return kRulerH + i * kLaneH - m_vbar->value(); }
int Timeline::laneAt(int y) const {
    if (y < kRulerH) return -1;
    return (y - kRulerH + m_vbar->value()) / kLaneH;
}
QRect Timeline::muteRect(int i) const { return QRect(10, laneY(i) + kLaneH - 28, 26, 18); }
QRect Timeline::soloRect(int i) const { return QRect(40, laneY(i) + kLaneH - 28, 26, 18); }

qint64 Timeline::snapped(qint64 f) const {
    if (!m_snap) return std::max<qint64>(0, f);
    const double g = fpb() / (m_easy ? 1.0 : 4.0);
    return std::max<qint64>(0, qint64(std::llround(std::round(f / g) * g)));
}

void Timeline::refreshLayout() {
    const int W = width() - m_vbar->width(), H = height() - m_hbar->height();
    const double endBeats = std::max(64.0, double(m_e->projectEnd()) / fpb() + 16);
    m_hbar->setRange(0, std::max(0, int(endBeats * m_ppb) - (W - kHeaderW)));
    m_hbar->setPageStep(std::max(1, W - kHeaderW));
    m_vbar->setRange(0, std::max(0, kRulerH + m_e->trackCount() * kLaneH - H));
    m_vbar->setPageStep(std::max(1, H - kRulerH));
    update();
}

void Timeline::followPlayhead() {
    const int W = width() - m_vbar->width();
    const int x = frameToX(m_e->position());
    if (x > W - 20 || x < kHeaderW) m_hbar->setValue(m_hbar->value() + x - kHeaderW - 40);
}

void Timeline::resizeEvent(QResizeEvent*) {
    const int vw = m_vbar->sizeHint().width(), hh = m_hbar->sizeHint().height();
    m_hbar->setGeometry(kHeaderW, height() - hh, width() - vw - kHeaderW, hh);
    m_vbar->setGeometry(width() - vw, kRulerH, vw, height() - hh - kRulerH);
    refreshLayout();
}

void Timeline::paintEvent(QPaintEvent*) {
    const auto tr = m_e->snapshot();
    QPainter p(this);
    const QColor text = palette().color(QPalette::Text);
    const QColor accent(255, 159, 28);
    const int W = width() - m_vbar->width(), H = height() - m_hbar->height();
    const int scrollX = m_hbar->value();
    p.fillRect(rect(), palette().color(QPalette::Base));

    // 레인 배경 + 그리드
    p.setClipRect(0, kRulerH, W, H - kRulerH);
    for (int i = 0; i < int(tr.size()); ++i) {
        const int y = laneY(i);
        p.fillRect(kHeaderW, y, W - kHeaderW, kLaneH - 1,
                   palette().color(i % 2 ? QPalette::AlternateBase : QPalette::Base));
    }
    const int b0 = std::max(0, int(std::floor(scrollX / m_ppb))), b1 = int((scrollX + W) / m_ppb) + 1;
    for (int b = b0; b <= b1; ++b) {
        const bool bar = b % 4 == 0;
        if (!bar && m_ppb < 10) continue;
        const int x = kHeaderW + int(b * m_ppb) - scrollX;
        if (x < kHeaderW) continue;
        QColor g = text; g.setAlpha(bar ? 70 : 28);
        p.setPen(g);
        p.drawLine(x, kRulerH, x, H);
    }

    // 클립 / 드럼 패턴
    for (int i = 0; i < int(tr.size()); ++i) {
        const int y = laneY(i);
        if (y + kLaneH < kRulerH || y > H) continue;
        const Track& t = tr[size_t(i)];
        p.setClipRect(kHeaderW, std::max(kRulerH, y), W - kHeaderW, kLaneH);
        if (t.drum) {
            const double stepPx = m_ppb / 4.0;
            const int k0 = std::max(0, int(std::floor(scrollX / stepPx))), k1 = int((scrollX + W) / stepPx) + 1;
            QColor c = t.color; c.setAlpha(150);
            for (int k = k0; k <= k1; ++k) {
                int cnt = 0;
                for (int r = 0; r < kDrumRows; ++r) cnt += t.steps[size_t(r)][size_t(k % kSteps)] ? 1 : 0;
                if (!cnt) continue;
                const int h = cnt * (kLaneH - 16) / kDrumRows;
                p.fillRect(kHeaderW + int(k * stepPx) - scrollX, y + kLaneH - 8 - h, std::max(2, int(stepPx) - 2), h, c);
            }
            continue;
        }
        for (size_t ci = 0; ci < t.clips.size(); ++ci) {
            const Clip& c = t.clips[ci];
            const int x0 = frameToX(c.start), x1 = frameToX(c.start + c.length);
            if (x1 < kHeaderW || x0 > W) continue;
            const QRect r(x0, y + 3, std::max(2, x1 - x0), kLaneH - 7);
            const bool sel = (i == m_selTrack && int(ci) == m_selClip);
            p.setPen(sel ? QPen(Qt::white, 2) : QPen(t.color.lighter(130)));
            p.setBrush(QColor(t.color.red(), t.color.green(), t.color.blue(), sel ? 120 : 80));
            p.drawRoundedRect(r, 4, 4);
            // 파형
            p.setPen(t.color.lighter(150));
            const double fpp = fpb() / m_ppb;
            const int mid = r.center().y(), amp = r.height() / 2 - 3;
            const int xa = std::max(r.left() + 1, kHeaderW), xb = std::min(r.right() - 1, W);
            const auto& pk = c.audio->peaks;
            for (int x = xa; x < xb; ++x) {
                const qint64 f0 = c.offset + qint64((x - x0) * fpp);
                const size_t i0 = size_t(f0 / kPeakStride), i1 = size_t((f0 + qint64(fpp)) / kPeakStride);
                float m = 0;
                for (size_t k = i0; k <= i1 && k < pk.size(); ++k) m = std::max(m, pk[k]);
                const int h = int(std::min(1.f, m) * amp);
                p.drawLine(x, mid - h, x, mid + h);
            }
            p.setPen(text);
            p.drawText(r.adjusted(6, 2, -4, -2), Qt::AlignTop | Qt::AlignLeft,
                       fontMetrics().elidedText(c.audio->name, Qt::ElideRight, r.width() - 10));
        }
    }

    // 트랙 헤더
    p.setClipRect(0, kRulerH, kHeaderW, H - kRulerH);
    for (int i = 0; i < int(tr.size()); ++i) {
        const int y = laneY(i);
        const Track& t = tr[size_t(i)];
        p.fillRect(0, y, kHeaderW - 1, kLaneH - 1, palette().color(QPalette::Window));
        p.fillRect(0, y, 5, kLaneH - 1, t.color);
        if (i == m_selTrack) { p.setPen(accent); p.setBrush(Qt::NoBrush); p.drawRect(0, y, kHeaderW - 2, kLaneH - 2); }
        QFont f = font(); f.setBold(true); p.setFont(f);
        p.setPen(text);
        p.drawText(QRect(12, y + 6, kHeaderW - 20, 22), Qt::AlignLeft | Qt::AlignVCenter,
                   QFontMetrics(f).elidedText(t.name, Qt::ElideRight, kHeaderW - 20));
        p.setFont(font());
        auto btn = [&](const QRect& r, const QString& s, bool on, const QColor& c) {
            p.setPen(on ? Qt::black : text);
            p.setBrush(on ? c : palette().color(QPalette::Button));
            p.drawRoundedRect(r, 3, 3);
            p.drawText(r, Qt::AlignCenter, s);
        };
        btn(muteRect(i), "M", t.mute, QColor(255, 150, 60));
        if (!m_easy) btn(soloRect(i), "S", t.solo, QColor(250, 220, 70));
    }

    // 눈금자
    p.setClipping(false);
    p.fillRect(0, 0, W, kRulerH, palette().color(QPalette::Window));
    if (m_e->loopOn()) {
        const int la = std::max(kHeaderW, frameToX(m_e->loopA())), lb = std::min(W, frameToX(m_e->loopB()));
        if (lb > la) p.fillRect(la, 0, lb - la, kRulerH, QColor(255, 159, 28, 90));
    }
    p.setClipRect(kHeaderW, 0, W - kHeaderW, kRulerH);
    const int barStep = (m_ppb * 4 < 30) ? 4 : 1;
    for (int b = b0; b <= b1; ++b) {
        const int x = kHeaderW + int(b * m_ppb) - scrollX;
        const bool bar = b % 4 == 0;
        if (!bar && m_ppb < 10) continue;
        p.setPen(text);
        p.drawLine(x, bar ? 4 : kRulerH - 8, x, kRulerH);
        if (bar && (b / 4) % barStep == 0) p.drawText(x + 4, 16, QString::number(b / 4 + 1));
    }

    // 재생 헤드
    p.setClipRect(kHeaderW, 0, W - kHeaderW, H);
    const int hx = frameToX(m_e->position());
    p.setPen(QPen(QColor(255, 80, 80), 1));
    p.drawLine(hx, 0, hx, H);
    p.setBrush(QColor(255, 80, 80));
    p.drawPolygon(QPolygon({QPoint(hx - 5, 0), QPoint(hx + 5, 0), QPoint(hx, 9)}));
    p.setClipping(false);
    p.fillRect(0, 0, kHeaderW, kRulerH, palette().color(QPalette::Window));
}

void Timeline::mousePressEvent(QMouseEvent* e) {
    setFocus();
    const QPoint pt = e->position().toPoint();
    const int W = width() - m_vbar->width();
    if (pt.x() > W) return;

    // 눈금자: 좌클릭=재생 위치 이동, 우클릭 드래그=루프 구간 지정
    if (pt.y() < kRulerH && pt.x() >= kHeaderW) {
        if (e->button() == Qt::LeftButton) { m_dragHead = true; m_e->seek(std::max<qint64>(0, xToFrame(pt.x()))); }
        else if (e->button() == Qt::RightButton) {
            m_dragLoop = true;
            const double b = fpb();
            m_loopAnchor = qint64(std::llround(std::round(xToFrame(pt.x()) / b) * b));
        }
        return;
    }
    const int ti = laneAt(pt.y());
    auto tr = m_e->snapshot();
    if (ti < 0 || ti >= int(tr.size())) return;

    // 트랙 헤더
    if (pt.x() < kHeaderW) {
        m_selTrack = ti; m_selClip = -1;
        if (e->button() == Qt::RightButton) {
            QMenu menu(this);
            QAction* ren = menu.addAction("이름 바꾸기");
            QAction* del = ti >= 1 ? menu.addAction("트랙 삭제") : nullptr;
            QAction* a = menu.exec(e->globalPosition().toPoint());
            if (a && a == ren) {
                bool ok = false;
                const QString n = QInputDialog::getText(this, "이름 바꾸기", "트랙 이름", QLineEdit::Normal, tr[size_t(ti)].name, &ok);
                if (ok && !n.isEmpty()) m_e->renameTrack(ti, n);
            } else if (a && a == del) { m_selTrack = -1; m_e->removeTrack(ti); }
        } else if (muteRect(ti).contains(pt)) m_e->setMute(ti, !tr[size_t(ti)].mute);
        else if (!m_easy && soloRect(ti).contains(pt)) m_e->setSolo(ti, !tr[size_t(ti)].solo);
        update();
        return;
    }

    // 클립 선택/드래그 시작
    m_selTrack = ti; m_selClip = -1;
    if (e->button() == Qt::LeftButton) {
        const qint64 f = xToFrame(pt.x());
        const auto& cl = tr[size_t(ti)].clips;
        for (int ci = int(cl.size()) - 1; ci >= 0; --ci) {
            if (f >= cl[size_t(ci)].start && f < cl[size_t(ci)].start + cl[size_t(ci)].length) {
                m_selClip = ci; m_dragOff = f - cl[size_t(ci)].start; m_dragClip = true; break;
            }
        }
    }
    update();
}

void Timeline::mouseMoveEvent(QMouseEvent* e) {
    const int x = int(e->position().x());
    if (m_dragHead) m_e->seek(std::max<qint64>(0, xToFrame(x)));
    else if (m_dragClip) m_e->moveClip(m_selTrack, m_selClip, snapped(xToFrame(x) - m_dragOff));
    else if (m_dragLoop) {
        const double b = fpb();
        const qint64 cur = qint64(std::llround(std::round(std::max<qint64>(0, xToFrame(x)) / b) * b));
        const qint64 a = std::min(m_loopAnchor, cur), c = std::max(m_loopAnchor, cur);
        if (c - a >= qint64(b)) m_e->setLoop(true, a, c);
    }
}

void Timeline::mouseReleaseEvent(QMouseEvent*) { m_dragHead = m_dragClip = m_dragLoop = false; }

void Timeline::wheelEvent(QWheelEvent* e) {
    const int dy = e->angleDelta().y(), dx = e->angleDelta().x();
    if (e->modifiers() & Qt::ControlModifier) {
        const int x = int(e->position().x());
        const double beatAtCursor = (x - kHeaderW + m_hbar->value()) / m_ppb;
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

void Timeline::keyPressEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Delete || e->key() == Qt::Key_Backspace) {
        if (m_selClip >= 0) { m_e->removeClip(m_selTrack, m_selClip); m_selClip = -1; }
    } else if (e->key() == Qt::Key_S && m_selClip >= 0) {
        m_e->splitClip(m_selTrack, m_selClip, m_e->position());
    } else {
        QWidget::keyPressEvent(e);
    }
}

void Timeline::dragEnterEvent(QDragEnterEvent* e) {
    if (e->mimeData()->hasUrls()) e->acceptProposedAction();
}

void Timeline::dropEvent(QDropEvent* e) {
    const int ti = laneAt(int(e->position().y()));
    const qint64 f = snapped(xToFrame(int(e->position().x())));
    for (const QUrl& u : e->mimeData()->urls()) {
        const QString path = u.toLocalFile();
        if (path.isEmpty()) continue;
        m_e->importAudio(path, (ti >= 1 && ti < m_e->trackCount()) ? ti : -1, f);
    }
    e->acceptProposedAction();
}
