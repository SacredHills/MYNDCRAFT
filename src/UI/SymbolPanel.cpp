#include "UI/SymbolPanel.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QToolButton>
#include <QVBoxLayout>

namespace mynd {

SymbolPanel::SymbolPanel(EditorSession* s, SymbolActions* a, QWidget* parent) : QWidget(parent), m_s(s), m_a(a) {
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(2, 2, 2, 2);
    v->setSpacing(2);
    auto* bar = new QHBoxLayout;
    bar->setSpacing(4);
    for (QAction* act : {a->convert, a->newSymbol, a->insertInstance, a->editSymbol, a->exitSymbol, a->renameSymbol, a->deleteSymbol}) {
        auto* b = new QToolButton;
        b->setDefaultAction(act);
        b->setToolButtonStyle(Qt::ToolButtonTextOnly);
        b->setAutoRaise(true);
        bar->addWidget(b);
    }
    bar->addStretch(1);
    v->addLayout(bar);
    m_list = new QListWidget;
    m_list->setAlternatingRowColors(true);
    m_list->setToolTip(tr("Symbols in this project. Double-click to edit one; select one and use Insert to place an instance."));
    v->addWidget(m_list, 1);
    auto* hint = new QLabel(tr("Tip: select a layer and press F8 to turn it into a symbol. Double-click an instance on the canvas to edit it."));
    hint->setWordWrap(true);
    hint->setStyleSheet("color:#8a8a90;");
    v->addWidget(hint);

    connect(m_list, &QListWidget::currentItemChanged, this, [this] {
        if (m_busy) return;
        const QListWidgetItem* it = m_list->currentItem();
        m_a->setLibrarySelection(it ? QUuid(it->data(Qt::UserRole).toString()) : QUuid());
    });
    connect(m_list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* it) {
        m_a->setLibrarySelection(QUuid(it->data(Qt::UserRole).toString()));
        m_a->editTarget();
    });
    connect(s, &EditorSession::documentChanged, this, [this] {
        disconnect(m_c1); disconnect(m_c2); disconnect(m_c3);
        if (Document* d = m_s->doc()) {
            m_c1 = connect(d, &Document::symbolsChanged, this, [this] { rebuild(); });
            m_c2 = connect(d, &Document::layerTreeChanged, this, [this] { rebuild(); });   // instance counts
            m_c3 = connect(d, &Document::contextChanged, this, [this] { rebuild(); });
        }
        rebuild();
    });
    rebuild();
}

void SymbolPanel::rebuild() {
    m_busy = true;
    const QUuid keep = m_a->targetSymbol();
    m_list->clear();
    if (const Document* d = m_s->doc()) {
        for (const SymbolDef& sd : d->symbols()) {
            const int uses = d->symbolUseCount(sd.id);
            auto* it = new QListWidgetItem(tr("%1   —   %2, %3 frames, %4 instance(s)")
                                               .arg(sd.name, sd.kind == SymbolKind::Graphic ? tr("Graphic") : tr("Movie Clip"))
                                               .arg(sd.durationFrames).arg(uses));
            it->setData(Qt::UserRole, sd.id.toString());
            m_list->addItem(it);
            if (sd.id == keep) m_list->setCurrentItem(it);
        }
    }
    m_busy = false;
}

}  // namespace mynd
