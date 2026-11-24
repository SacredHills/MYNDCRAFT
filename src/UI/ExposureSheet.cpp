#include "UI/ExposureSheet.h"

#include <QBrush>
#include <QHeaderView>
#include <QMenu>
#include <QToolBar>
#include <QVBoxLayout>

namespace mynd {

namespace {
QColor markerBg(PoseMarker m) {
    switch (m) {
    case PoseMarker::Key: return QColor(0xc0, 0x39, 0x2b, 170);
    case PoseMarker::Breakdown: return QColor(0xd6, 0x89, 0x10, 170);
    case PoseMarker::Inbetween: return QColor(0x27, 0xae, 0x60, 150);
    case PoseMarker::None: break;
    }
    return QColor(0x4a, 0x6a, 0x8a, 140);
}
}  // namespace

ExposureModel::ExposureModel(EditorSession* s, QObject* parent) : QAbstractTableModel(parent), m_s(s) {
    auto hook = [this] {
        if (Document* d = m_s->doc()) {
            connect(d, &Document::layerTreeChanged, this, &ExposureModel::reload);
            connect(d, &Document::layerPropsChanged, this, [this] { reload(); });
            connect(d, &Document::animationChanged, this, [this] { reload(); });
            connect(d, &Document::timingChanged, this, &ExposureModel::reload);
        }
        reload();
    };
    connect(s, &EditorSession::documentChanged, this, hook);
    hook();
}

void ExposureModel::reload() {
    beginResetModel();
    m_layers.clear();
    if (Document* d = m_s->doc()) {
        std::function<void(const Layer*)> walk = [&](const Layer* g) {
            for (int i = int(g->children.size()) - 1; i >= 0; --i) {
                const Layer* c = g->children[i].get();
                if (c->type == LayerType::Animation) m_layers.append(c->id);
                if (c->isGroup()) walk(c);
            }
        };
        walk(d->root());
    }
    endResetModel();
}

int ExposureModel::rowCount(const QModelIndex&) const { return m_s->doc() ? m_s->doc()->durationFrames() : 0; }

Qt::ItemFlags ExposureModel::flags(const QModelIndex& i) const {
    Qt::ItemFlags f = QAbstractTableModel::flags(i);
    const Layer* l = m_s->doc() ? m_s->doc()->findLayer(layerAt(i.column())) : nullptr;
    if (l && !l->props.locked) f |= Qt::ItemIsEditable;
    return f;
}

QVariant ExposureModel::headerData(int section, Qt::Orientation o, int role) const {
    if (o == Qt::Vertical) {
        if (role == Qt::DisplayRole) return section + 1;
        return {};
    }
    const Layer* l = m_s->doc() ? m_s->doc()->findLayer(layerAt(section)) : nullptr;
    if (!l) return {};
    if (role == Qt::DisplayRole) return l->props.name;
    if (role == Qt::ToolTipRole) return tr("%1 (%2 drawings)").arg(l->props.name).arg(l->anim.drawings.size());
    return {};
}

QVariant ExposureModel::data(const QModelIndex& i, int role) const {
    const Layer* l = m_s->doc() ? m_s->doc()->findLayer(layerAt(i.column())) : nullptr;
    if (!l) return {};
    const Exposure* e = l->anim.at(i.row());
    switch (role) {
    case Qt::DisplayRole:
    case Qt::EditRole:
        if (!e) return role == Qt::EditRole ? QVariant(QString()) : QVariant();
        if (e->drawing.isNull()) return role == Qt::EditRole ? QString("x") : QString("×");
        if (e->start == i.row() || role == Qt::EditRole) {
            const DrawingMeta* d = l->anim.drawing(e->drawing);
            return d ? d->name : QString("?");
        }
        return QString("│");
    case Qt::TextAlignmentRole: return int(Qt::AlignCenter);
    case Qt::BackgroundRole:
        if (!e) return {};
        if (e->drawing.isNull()) return QBrush(QColor(90, 90, 90, 120), Qt::BDiagPattern);
        return QBrush(markerBg(e->marker));
    case Qt::FontRole:
        if (e && e->start == i.row()) { QFont f; f.setBold(true); return f; }
        return {};
    case Qt::ToolTipRole:
        if (!e) return tr("Empty frame — type a drawing name to expose it here");
        if (e->drawing.isNull()) return tr("Blank exposure (%1 frame(s))").arg(e->length);
        if (const DrawingMeta* d = l->anim.drawing(e->drawing)) {
            QString s = tr("Drawing %1, frames %2–%3 (%4 frame(s))").arg(d->name).arg(e->start + 1).arg(e->end()).arg(e->length);
            if (e->marker != PoseMarker::None) s += "\n" + tr("Pose: %1").arg(poseMarkerKey(e->marker));
            return s;
        }
        return {};
    }
    return {};
}

bool ExposureModel::setData(const QModelIndex& i, const QVariant& v, int role) {
    if (role != Qt::EditRole) return false;
    Document* d = m_s->doc();
    Layer* l = d ? d->findLayer(layerAt(i.column())) : nullptr;
    if (!l || l->props.locked) return false;
    const QString text = v.toString().trimmed();
    const int frame = i.row();
    AnimationState s = l->anim;
    QUuid newImageFor;      // set when a brand-new drawing needs pixels
    QString newName;
    if (text.isEmpty()) {
        s.clearRange(frame, 1);
    } else if (text == "x" || text == "X" || text == "×" || text == "-") {
        s.setRange(frame, 1, QUuid());
    } else {
        QUuid id;
        for (const auto& dm : s.drawings) if (dm.name == text) id = dm.id;
        if (id.isNull()) {
            // New name: create the drawing (pixels included) in one undoable step.
            const QUuid created = d->createDrawing(l->id, frame, false, -1, text);
            return !created.isNull();
        }
        s.setRange(frame, 1, id);
    }
    s.normalize();
    if (s == l->anim) return false;
    d->editAnimation(l->id, s, tr("Edit Exposure"));
    return true;
}

// ============================================================ panel
ExposureSheetPanel::ExposureSheetPanel(EditorSession* s, AnimationActions* a, QWidget* parent)
    : QWidget(parent), m_s(s), m_a(a) {
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(2, 2, 2, 2);
    auto* tb = new QToolBar;
    tb->setIconSize(QSize(16, 16));
    tb->setToolButtonStyle(Qt::ToolButtonTextOnly);
    for (QAction* act : {a->newDrawing, a->duplicateDrawing, a->extend, a->shorten, a->insertBlank, a->deleteFrame, a->duplicateExposure}) {
        tb->addAction(act);
        if (QWidget* w = tb->widgetForAction(act)) w->setToolTip(act->text() + (act->shortcut().isEmpty() ? QString() : "  (" + act->shortcut().toString(QKeySequence::NativeText) + ")"));
    }
    auto* pose = new QMenu(tr("Pose"), tb);
    pose->addActions({a->markKey, a->markBreakdown, a->markInbetween, a->markClear});
    tb->addAction(pose->menuAction());
    lay->addWidget(tb);

    m_model = new ExposureModel(s, this);
    m_table = new QTableView;
    m_table->setModel(m_model);
    m_table->setSelectionMode(QAbstractItemView::ContiguousSelection);
    m_table->setSelectionBehavior(QAbstractItemView::SelectItems);
    m_table->verticalHeader()->setDefaultSectionSize(20);
    m_table->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_table->horizontalHeader()->setMinimumSectionSize(48);
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    m_table->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed | QAbstractItemView::AnyKeyPressed);
    lay->addWidget(m_table, 1);

    connect(m_table->selectionModel(), &QItemSelectionModel::currentChanged, this, [this](const QModelIndex& cur) {
        if (m_busy || !cur.isValid() || !m_s->doc()) return;
        m_busy = true;
        m_s->setActiveLayer(m_model->layerAt(cur.column()));
        m_s->doc()->setCurrentFrame(cur.row());
        m_busy = false;
    });
    // Dragging / Shift+click over several frames of one layer selects them for Copy / Cut / Paste / Delete,
    // exactly like the same range on the timeline.
    connect(m_table->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this] {
        if (m_busy || !m_s->doc()) return;
        const QModelIndexList sel = m_table->selectionModel()->selectedIndexes();
        if (sel.size() < 2) return;
        int col = sel.first().column(), lo = sel.first().row(), hi = lo;
        for (const QModelIndex& i : sel) { if (i.column() != col) return; lo = qMin(lo, i.row()); hi = qMax(hi, i.row()); }
        const QUuid layer = m_model->layerAt(col);
        if (layer.isNull()) return;
        m_busy = true;
        m_s->setActiveLayer(layer);
        m_s->setFrameSelection(layer, lo, hi);
        m_busy = false;
    });
    connect(s, &EditorSession::frameSelectionChanged, this, [this] {
        if (m_busy || !m_s->hasFrameSelection()) return;
        const int col = m_model->columnOf(m_s->frameSelectionLayer());
        if (col < 0) return;
        int a, b; m_s->selectedFrameRange(&a, &b);
        const int rows = m_model->rowCount();
        if (rows == 0) return;
        m_busy = true;
        m_table->selectionModel()->select(QItemSelection(m_model->index(qBound(0, a, rows - 1), col), m_model->index(qBound(0, b, rows - 1), col)),
                                          QItemSelectionModel::ClearAndSelect);
        m_busy = false;
    });
    connect(m_table, &QTableView::customContextMenuRequested, this, [this](const QPoint& pt) {
        const QModelIndex i = m_table->indexAt(pt);
        if (i.isValid() && !m_table->selectionModel()->isSelected(i)) m_table->setCurrentIndex(i);
        QMenu menu(this);
        m_a->fillContextMenu(&menu);
        menu.exec(m_table->viewport()->mapToGlobal(pt));
    });
    auto sync = [this] { syncSelection(); };
    connect(s, &EditorSession::activeLayerChanged, this, sync);
    connect(s, &EditorSession::documentChanged, this, [this, sync] {
        if (Document* d = m_s->doc()) connect(d, &Document::frameChanged, this, [this] { syncSelection(); });
        syncSelection();
    });
    connect(m_model, &QAbstractItemModel::modelReset, this, sync);
    syncSelection();
}

void ExposureSheetPanel::syncSelection() {
    if (m_busy) return;
    m_busy = true;
    Document* d = m_s->doc();
    int col = m_model->columnOf(m_s->activeLayerId());
    if (col < 0 && m_model->columnCount() > 0) col = m_table->currentIndex().isValid() ? m_table->currentIndex().column() : 0;
    if (d && col >= 0 && d->currentFrame() < m_model->rowCount()) {
        const QModelIndex idx = m_model->index(d->currentFrame(), col);
        m_table->setCurrentIndex(idx);
        m_table->scrollTo(idx, QAbstractItemView::EnsureVisible);
    }
    m_busy = false;
}

}  // namespace mynd
