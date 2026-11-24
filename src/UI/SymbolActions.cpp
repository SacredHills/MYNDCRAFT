#include "UI/SymbolActions.h"

#include "Core/ProjectIO.h"

#include <QFileDialog>
#include <QInputDialog>
#include <QMessageBox>

namespace mynd {

namespace {
QString nextName(const Document& d, const QString& base) {
    for (int i = 1;; ++i) {
        const QString n = QString("%1 %2").arg(base).arg(i);
        bool used = false;
        for (const auto& s : d.symbols()) used |= s.name == n;
        if (!used) return n;
    }
}
bool askNameAndKind(QWidget* parent, const QString& title, QString* name, SymbolKind* kind, const QString& suggested) {
    bool ok = false;
    const QString n = QInputDialog::getText(parent, title, QObject::tr("Symbol name:"), QLineEdit::Normal, suggested, &ok);
    if (!ok || n.trimmed().isEmpty()) return false;
    const QStringList kinds{QObject::tr("Graphic (follows the timeline it is placed in)"), QObject::tr("Movie Clip (plays on its own and loops)")};
    const QString k = QInputDialog::getItem(parent, title, QObject::tr("Type:"), kinds, 0, false, &ok);
    if (!ok) return false;
    *name = n.trimmed();
    *kind = k == kinds[1] ? SymbolKind::MovieClip : SymbolKind::Graphic;
    return true;
}
}  // namespace

SymbolActions::SymbolActions(EditorSession* s, QWidget* dialogParent) : QObject(dialogParent), m_s(s), m_parent(dialogParent) {
    auto mk = [this](const QString& text, const QKeySequence& key = {}) {
        auto* a = new QAction(text, this);
        if (!key.isEmpty()) a->setShortcut(key);
        a->setShortcutContext(Qt::WindowShortcut);
        return a;
    };
    convert = mk(tr("Convert Layer to Symbol…"), QKeySequence("F8"));
    connect(convert, &QAction::triggered, this, [this] {
        const Layer* l = m_s->activeLayer();
        if (!l || !m_s->doc()) return;
        QString n; SymbolKind k;
        if (!askNameAndKind(m_parent, tr("Convert to Symbol"), &n, &k, nextName(*m_s->doc(), tr("Symbol")))) return;
        if (convertActive(n, k).isNull()) QMessageBox::warning(m_parent, tr("Convert to Symbol"), tr("This layer cannot become a symbol here (a symbol cannot contain itself)."));
    });
    newSymbol = mk(tr("New Empty Symbol…"));
    connect(newSymbol, &QAction::triggered, this, [this] {
        if (!m_s->doc()) return;
        QString n; SymbolKind k;
        if (!askNameAndKind(m_parent, tr("New Symbol"), &n, &k, nextName(*m_s->doc(), tr("Symbol")))) return;
        const QUuid id = makeNewSymbol(n, k);
        if (!id.isNull()) { setLibrarySelection(id); editTarget(); }
    });
    insertInstance = mk(tr("Insert Instance of Symbol"));
    connect(insertInstance, &QAction::triggered, this, [this] {
        if (insertTarget().isNull()) emit m_s->message(tr("Pick a symbol in the Library tab first (a symbol cannot be placed inside itself)."));
    });
    editSymbol = mk(tr("Edit Symbol"), QKeySequence("Ctrl+Shift+Down"));
    connect(editSymbol, &QAction::triggered, this, [this] { if (!editTarget()) emit m_s->message(tr("Select a symbol instance or pick a symbol in the Library tab.")); });
    exitSymbol = mk(tr("Exit Symbol (Back to Scene)"), QKeySequence("Ctrl+Shift+Up"));
    connect(exitSymbol, &QAction::triggered, this, [this] { leave(); });
    renameSymbol = mk(tr("Rename Symbol…"));
    connect(renameSymbol, &QAction::triggered, this, [this] {
        const SymbolDef* d = m_s->doc() ? m_s->doc()->findSymbol(targetSymbol()) : nullptr;
        if (!d) return;
        bool ok = false;
        const QString n = QInputDialog::getText(m_parent, tr("Rename Symbol"), tr("Symbol name:"), QLineEdit::Normal, d->name, &ok);
        if (ok) m_s->doc()->renameSymbol(d->id, n);
    });
    deleteSymbol = mk(tr("Delete Symbol"));
    connect(deleteSymbol, &QAction::triggered, this, [this] {
        QString why;
        if (!removeTarget(&why) && !why.isEmpty()) emit m_s->message(why);
    });
    exportSymbolAction = mk(tr("Export Symbol as Asset…"));
    connect(exportSymbolAction, &QAction::triggered, this, [this] {
        const SymbolDef* d = m_s->doc() ? m_s->doc()->findSymbol(targetSymbol()) : nullptr;
        if (!d) return;
        const QString path = QFileDialog::getSaveFileName(m_parent, tr("Export Symbol"), d->name + ".studio", tr("MyndCraft projects (*.studio)"));
        if (path.isEmpty()) return;
        QString err;
        if (!exportTarget(path, &err)) QMessageBox::warning(m_parent, tr("Export Symbol"), err);
        else emit m_s->message(tr("Exported symbol to %1").arg(path));
    });
    importSymbolsAction = mk(tr("Import Symbols from Project…"));
    connect(importSymbolsAction, &QAction::triggered, this, [this] {
        const QString path = QFileDialog::getOpenFileName(m_parent, tr("Import Symbols"), QString(), tr("MyndCraft projects (*.studio)"));
        if (path.isEmpty()) return;
        QString err;
        const QVector<QUuid> ids = importFrom(path, &err);
        if (ids.isEmpty()) QMessageBox::information(m_parent, tr("Import Symbols"), err.isEmpty() ? tr("That project has no symbols.") : err);
        else emit m_s->message(tr("Imported %1 symbol(s).").arg(ids.size()));
    });
    connect(m_s, &EditorSession::activeLayerChanged, this, &SymbolActions::refresh);
    connect(m_s, &EditorSession::documentChanged, this, [this] {
        if (Document* d = m_s->doc()) {
            connect(d, &Document::symbolsChanged, this, &SymbolActions::refresh);
            connect(d, &Document::contextChanged, this, &SymbolActions::refresh);
        }
        refresh();
    });
    refresh();
}

void SymbolActions::fillMenu(QMenu* m) {
    m->addActions({convert, newSymbol});
    m->addSeparator();
    m->addActions({insertInstance, editSymbol, exitSymbol});
    m->addSeparator();
    m->addActions({renameSymbol, deleteSymbol});
    m->addSeparator();
    m->addActions({exportSymbolAction, importSymbolsAction});
}

void SymbolActions::setLibrarySelection(const QUuid& id) { m_sel = id; refresh(); }

QUuid SymbolActions::targetSymbol() const {
    const Document* d = m_s->doc();
    if (!d) return {};
    if (d->findSymbol(m_sel)) return m_sel;
    const Layer* l = m_s->activeLayer();
    return l && l->type == LayerType::Symbol ? l->symbolId : QUuid();
}

QUuid SymbolActions::convertActive(const QString& name, SymbolKind kind) {
    const Layer* l = m_s->activeLayer();
    if (!l || !m_s->doc() || l->props.locked) return {};
    return m_s->doc()->convertToSymbol(l->id, name, kind);
}

QUuid SymbolActions::makeNewSymbol(const QString& name, SymbolKind kind) {
    return m_s->doc() ? m_s->doc()->createSymbol(name, kind, m_s->doc()->durationFrames()) : QUuid();
}

QUuid SymbolActions::insertTarget() {
    Document* d = m_s->doc();
    const QUuid sym = d && d->findSymbol(m_sel) ? m_sel : QUuid();
    if (!d || sym.isNull() || !d->canInstantiate(sym)) return {};
    QUuid parent; int index = -1;
    if (const Layer* a = m_s->activeLayer()) {
        if (a->isGroup()) parent = a->id;
        else { parent = a->parent && a->parent != d->root() ? a->parent->id : QUuid(); index = d->indexInParent(a) + 1; }
    }
    const QUuid id = d->addSymbolInstance(sym, parent, index);
    if (!id.isNull()) m_s->setActiveLayer(id);
    return id;
}

bool SymbolActions::editTarget() {
    const QUuid t = targetSymbol();
    if (t.isNull() || !m_s->doc()) return false;
    m_s->doc()->enterSymbol(t);
    return m_s->doc()->editingSymbolId() == t;
}

bool SymbolActions::leave() {
    if (!m_s->doc() || !m_s->doc()->editingSymbol()) return false;
    m_s->doc()->exitSymbol();
    return true;
}

bool SymbolActions::removeTarget(QString* why) {
    const QUuid t = targetSymbol();
    if (t.isNull() || !m_s->doc()) return false;
    const bool ok = m_s->doc()->deleteSymbol(t, why);
    if (ok) m_sel = {};
    return ok;
}

bool SymbolActions::exportTarget(const QString& path, QString* error) {
    if (!m_s->doc()) return false;
    auto asset = m_s->doc()->symbolAsset(targetSymbol());
    if (!asset) { if (error) *error = tr("No symbol selected."); return false; }
    return ProjectIO::saveCopy(*asset, path, error);
}

QVector<QUuid> SymbolActions::importFrom(const QString& path, QString* error) {
    if (!m_s->doc()) return {};
    LoadResult r = ProjectIO::load(path);
    if (!r.doc) { if (error) *error = r.error; return {}; }
    return m_s->doc()->importSymbols(*r.doc);
}

void SymbolActions::refresh() {
    const Document* d = m_s->doc();
    const Layer* l = m_s->activeLayer();
    const bool doc = d != nullptr;
    convert->setEnabled(doc && l && !l->props.locked);
    newSymbol->setEnabled(doc);
    const QUuid t = targetSymbol();
    editSymbol->setEnabled(doc && !t.isNull());
    exitSymbol->setEnabled(doc && d->editingSymbol());
    renameSymbol->setEnabled(doc && !t.isNull());
    exportSymbolAction->setEnabled(doc && !t.isNull());
    importSymbolsAction->setEnabled(doc);
    deleteSymbol->setEnabled(doc && !t.isNull());
    insertInstance->setEnabled(doc && d->findSymbol(m_sel) && d->canInstantiate(m_sel));
}

}  // namespace mynd
