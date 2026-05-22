#include "screens/settings/SkillDiffsSection.h"

#include "core/logging/Logger.h"
#include "storage/repositories/SettingsRepository.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QSplitter>
#include <QVBoxLayout>

namespace fincept::screens {

namespace {

constexpr const char* kSdSettingsKey = "skill_diffs.upstream_path";
constexpr const char* kSdSettingsCat = "ai";
constexpr const char* kSdTag = "SkillDiffsSection";

constexpr int kSdColName    = 0;
constexpr int kSdColKind    = 1;
constexpr int kSdColLocalSha = 2;
constexpr int kSdColUpSha   = 3;
constexpr int kSdColCount   = 4;

/// Resolve the in-app `vendor_skills_diff.py` script.  Same pattern
/// as TtsService — check the installed scripts/ dir first, then the
/// dev-build sibling.
QString resolve_script_path() {
    const QString app = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        app + "/scripts/agents/ci/vendor_skills_diff.py",
        app + "/../fincept-qt/scripts/agents/ci/vendor_skills_diff.py",
    };
    for (const auto& p : candidates) {
        if (QFileInfo::exists(p))
            return p;
    }
    return {};
}

/// The vendored skills tree — local SKILL.md writes must canonicalise
/// underneath this.  Same candidate search as resolve_script_path so
/// dev + installed layouts both work.  Returns canonical path or
/// empty.
QString resolve_local_skills_root() {
    const QString app = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        app + "/scripts/agents/finagent_core/skills",
        app + "/../fincept-qt/scripts/agents/finagent_core/skills",
    };
    for (const auto& p : candidates) {
        if (QFileInfo(p).isDir())
            return QFileInfo(p).canonicalFilePath();
    }
    return {};
}

} // anonymous namespace

SkillDiffsSection::SkillDiffsSection(QWidget* parent) : QWidget(parent) {
    build_ui();
    load_upstream_path();
    on_selection_changed();
}

void SkillDiffsSection::build_ui() {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(12);

    auto* hdr = new QLabel(QStringLiteral(
        "<b>Vendor skill drift</b> — compare finterm's bundled "
        "<code>SKILL.md</code> files against an upstream clone of "
        "<code>anthropics/financial-services</code>.  Review divergences "
        "row-by-row; per-row <b>Accept upstream</b> overwrites the local "
        "file (confirm required)."));
    hdr->setWordWrap(true);
    root->addWidget(hdr);

    auto* path_row = new QHBoxLayout;
    path_row->addWidget(new QLabel(QStringLiteral("Upstream clone:")));
    upstream_edit_ = new QLineEdit;
    upstream_edit_->setPlaceholderText(QStringLiteral("/path/to/anthropics/financial-services"));
    path_row->addWidget(upstream_edit_, 1);
    browse_btn_ = new QPushButton(QStringLiteral("Browse…"));
    connect(browse_btn_, &QPushButton::clicked, this, &SkillDiffsSection::on_browse);
    path_row->addWidget(browse_btn_);
    run_btn_ = new QPushButton(QStringLiteral("Run diff"));
    connect(run_btn_, &QPushButton::clicked, this, &SkillDiffsSection::on_run_diff);
    path_row->addWidget(run_btn_);
    root->addLayout(path_row);

    hint_lbl_ = new QLabel(QStringLiteral(
        "Configure the upstream path above, then click <b>Run diff</b>.  "
        "Results appear in the table — drift rows show local vs upstream sha "
        "and unlock the per-row actions."));
    hint_lbl_->setWordWrap(true);
    hint_lbl_->setStyleSheet(QStringLiteral("color:#888;"));
    root->addWidget(hint_lbl_);

    auto* action_row = new QHBoxLayout;
    view_diff_btn_ = new QPushButton(QStringLiteral("View diff"));
    accept_btn_ = new QPushButton(QStringLiteral("Accept upstream…"));
    action_row->addWidget(view_diff_btn_);
    action_row->addWidget(accept_btn_);
    action_row->addStretch();
    root->addLayout(action_row);

    connect(view_diff_btn_, &QPushButton::clicked, this, &SkillDiffsSection::on_view_diff);
    connect(accept_btn_, &QPushButton::clicked, this, &SkillDiffsSection::on_accept_upstream);

    table_ = new QTableWidget(this);
    table_->setColumnCount(kSdColCount);
    table_->setHorizontalHeaderLabels(
        {QStringLiteral("Skill"), QStringLiteral("State"),
         QStringLiteral("Local sha"), QStringLiteral("Upstream sha")});
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(kSdColName, QHeaderView::Stretch);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->verticalHeader()->setVisible(false);
    connect(table_, &QTableWidget::itemSelectionChanged,
            this, &SkillDiffsSection::on_selection_changed);
    root->addWidget(table_, 1);

    status_lbl_ = new QLabel;
    status_lbl_->setWordWrap(true);
    root->addWidget(status_lbl_);
}

void SkillDiffsSection::load_upstream_path() {
    auto r = SettingsRepository::instance().get(QString::fromUtf8(kSdSettingsKey));
    if (r.is_ok())
        upstream_edit_->setText(r.value().trimmed());
}

void SkillDiffsSection::save_upstream_path(const QString& path) {
    auto r = SettingsRepository::instance().set(
        QString::fromUtf8(kSdSettingsKey), path, QString::fromUtf8(kSdSettingsCat));
    if (r.is_err())
        LOG_WARN(kSdTag, QString("save_upstream_path: %1").arg(QString::fromStdString(r.error())));
}

void SkillDiffsSection::on_browse() {
    const QString chosen = QFileDialog::getExistingDirectory(
        this, QStringLiteral("Select anthropics/financial-services clone"),
        upstream_edit_->text());
    if (chosen.isEmpty())
        return;
    upstream_edit_->setText(chosen);
}

void SkillDiffsSection::on_run_diff() {
    if (diff_proc_) {
        show_status(QStringLiteral("Diff already in progress…"), true);
        return;
    }
    const QString upstream = upstream_edit_->text().trimmed();
    if (upstream.isEmpty()) {
        show_status(QStringLiteral("Configure the upstream path first."), true);
        return;
    }
    if (!QFileInfo(upstream).isDir()) {
        show_status(QStringLiteral("Not a directory: %1").arg(upstream), true);
        return;
    }
    const QString script = resolve_script_path();
    if (script.isEmpty()) {
        show_status(QStringLiteral("vendor_skills_diff.py not found in scripts/agents/ci/"), true);
        return;
    }

    // Persist now so the next launch already has the path.
    save_upstream_path(upstream);

    // Async: spawn QProcess, wire the finished signal to drive the
    // result handler off the UI event loop.  The synchronous
    // waitForFinished pattern was freezing the GUI for up to 30s
    // (Mutter started painting the "not responding" overlay), and
    // a single processEvents() pre-flush before the wait only
    // covered the queued repaints, not the long tail.
    diff_proc_ = new QProcess(this);
    diff_proc_->setProgram(QStringLiteral("python3"));
    diff_proc_->setArguments({script, "--upstream", upstream, "--json"});
    connect(diff_proc_, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int code, QProcess::ExitStatus) { on_diff_finished(code); });
    run_btn_->setEnabled(false);
    show_status(QStringLiteral("Running diff… (async)"));
    diff_proc_->start();
}

void SkillDiffsSection::on_diff_finished(int exit_code) {
    if (!diff_proc_)
        return;
    const QByteArray out = diff_proc_->readAllStandardOutput();
    const QByteArray err = diff_proc_->readAllStandardError();
    diff_proc_->deleteLater();
    diff_proc_ = nullptr;
    run_btn_->setEnabled(true);

    if (exit_code != 0 && exit_code != 1) {
        // 0 = clean, 1 = drift (still produces valid JSON), 2 = setup error.
        show_status(QStringLiteral("Diff failed (exit %1): %2")
                        .arg(exit_code).arg(QString::fromUtf8(err).left(300)), true);
        return;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(out);
    if (!doc.isObject()) {
        show_status(QStringLiteral("Diff produced non-JSON output (head: %1)")
                        .arg(QString::fromUtf8(out.left(200))), true);
        return;
    }
    last_report_ = doc.object();
    populate_table(last_report_);
    const QJsonArray diverged = last_report_.value(QStringLiteral("diverged")).toArray();
    const QJsonArray new_up = last_report_.value(QStringLiteral("new_upstream")).toArray();
    show_status(QStringLiteral("Done — %1 diverged, %2 new upstream, %3 vendored unchanged.")
                    .arg(diverged.size())
                    .arg(new_up.size())
                    .arg(last_report_.value(QStringLiteral("vendored")).toArray().size()));
    hint_lbl_->setVisible(diverged.isEmpty() && new_up.isEmpty());
}

void SkillDiffsSection::populate_table(const QJsonObject& report) {
    const QJsonArray diverged = report.value(QStringLiteral("diverged")).toArray();
    const QJsonArray new_up = report.value(QStringLiteral("new_upstream")).toArray();

    // Diverged rows expose local_path / upstream_path / sha pair.
    // new_upstream rows just have the name — we synthesise the local
    // path we'd write into so the per-row Accept can still do the
    // right thing.
    const QString repo_local_skills_rel = report.value(QStringLiteral("local_root")).toString();
    const QString app_root = QFileInfo(QCoreApplication::applicationDirPath()).absolutePath();

    int total = diverged.size() + new_up.size();
    table_->setRowCount(total);
    int row = 0;

    for (const auto& v : diverged) {
        const QJsonObject e = v.toObject();
        const QString name = e.value(QStringLiteral("name")).toString();
        auto* name_item = new QTableWidgetItem(name);
        name_item->setData(Qt::UserRole, e.value(QStringLiteral("local_path")).toString());
        name_item->setData(Qt::UserRole + 1, e.value(QStringLiteral("upstream_path")).toString());
        table_->setItem(row, kSdColName, name_item);
        table_->setItem(row, kSdColKind, new QTableWidgetItem(QStringLiteral("diverged")));
        table_->setItem(row, kSdColLocalSha,
                        new QTableWidgetItem(e.value(QStringLiteral("local_sha")).toString()));
        table_->setItem(row, kSdColUpSha,
                        new QTableWidgetItem(e.value(QStringLiteral("upstream_sha")).toString()));
        ++row;
    }

    for (const auto& v : new_up) {
        const QString name = v.toString();
        auto* name_item = new QTableWidgetItem(name);
        // No local path yet — leave blank.  Accept-upstream will refuse
        // (write target unknown without an existing vendored layout).
        name_item->setData(Qt::UserRole, QString());
        // Upstream path also unknown to the JSON output (only diverged
        // rows surface paths).  Best-effort — view-diff will surface
        // an empty-local file vs upstream-on-disk lookup that needs
        // both paths; without upstream_path we degrade gracefully.
        name_item->setData(Qt::UserRole + 1, QString());
        table_->setItem(row, kSdColName, name_item);
        table_->setItem(row, kSdColKind, new QTableWidgetItem(QStringLiteral("new_upstream")));
        table_->setItem(row, kSdColLocalSha, new QTableWidgetItem(QStringLiteral("—")));
        table_->setItem(row, kSdColUpSha, new QTableWidgetItem(QStringLiteral("—")));
        ++row;
    }
}

SkillDiffsSection::Paths SkillDiffsSection::paths_for_row(int row) {
    Paths out;
    auto* name_item = table_->item(row, kSdColName);
    if (!name_item)
        return out;
    out.name = name_item->text();
    const QString rel_local = name_item->data(Qt::UserRole).toString();
    const QString raw_upstream = name_item->data(Qt::UserRole + 1).toString();

    // Local-path allow-list — refuse any candidate whose canonical
    // resolution escapes the vendored skills tree.  Defends against a
    // tampered JSON report whose local_path is "../../../etc/passwd"
    // that happens to exist; without this the Accept-upstream flow
    // would overwrite arbitrary user-readable files.
    const QString local_root = resolve_local_skills_root();
    if (!rel_local.isEmpty() && !local_root.isEmpty()) {
        const QString app = QCoreApplication::applicationDirPath();
        const QStringList candidates = {
            app + "/" + rel_local,
            app + "/../" + rel_local,
            app + "/../../" + rel_local,
        };
        for (const auto& c : candidates) {
            if (!QFileInfo::exists(c))
                continue;
            const QString canon = QFileInfo(c).canonicalFilePath();
            if (canon.isEmpty())
                continue;
            // Must live under local_root with a trailing slash so
            // "skills_other/foo" doesn't prefix-match "skills/".
            if (canon.startsWith(local_root + QLatin1Char('/'))) {
                out.local = canon;
                break;
            }
        }
    }

    // Upstream-path allow-list — must canonicalise under the
    // configured upstream root from the last report.  Same defense
    // as above; without this Accept-upstream could read arbitrary
    // files (e.g. /etc/shadow) into a SKILL.md.
    if (!raw_upstream.isEmpty()) {
        const QString up_root_raw =
            last_report_.value(QStringLiteral("upstream_root")).toString();
        const QString up_root = up_root_raw.isEmpty()
            ? QString()
            : QFileInfo(up_root_raw).canonicalFilePath();
        const QString up_canon = QFileInfo(raw_upstream).canonicalFilePath();
        if (!up_canon.isEmpty() && !up_root.isEmpty()
            && up_canon.startsWith(up_root + QLatin1Char('/'))) {
            out.upstream = up_canon;
        }
    }
    return out;
}

void SkillDiffsSection::on_selection_changed() {
    const auto sel = table_->selectionModel()
        ? table_->selectionModel()->selectedRows()
        : QModelIndexList{};
    const bool has_selection = !sel.isEmpty();
    bool diverged = false;
    if (has_selection) {
        auto* kind = table_->item(sel.first().row(), kSdColKind);
        diverged = kind && kind->text() == QStringLiteral("diverged");
    }
    // View-diff + accept require both local and upstream paths (only
    // diverged rows have them).  new_upstream surfaces in the table
    // for awareness; the user still needs to manually bring the file
    // into the vendored tree.
    view_diff_btn_->setEnabled(diverged);
    accept_btn_->setEnabled(diverged);
}

void SkillDiffsSection::on_view_diff() {
    const auto sel = table_->selectionModel()->selectedRows();
    if (sel.isEmpty())
        return;
    Paths p = paths_for_row(sel.first().row());
    if (p.local.isEmpty() || p.upstream.isEmpty()) {
        show_status(QStringLiteral("Diff unavailable — missing local or upstream path"), true);
        return;
    }
    auto read = [](const QString& path) -> QString {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
            return QStringLiteral("(failed to read %1: %2)").arg(path, f.errorString());
        return QString::fromUtf8(f.readAll());
    };
    const QString local_text = read(p.local);
    const QString up_text = read(p.upstream);

    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("Diff — %1").arg(p.name));
    dlg.resize(1100, 700);
    auto* dl = new QVBoxLayout(&dlg);

    auto* meta = new QLabel(QStringLiteral(
        "<b>Local</b>: <code>%1</code><br><b>Upstream</b>: <code>%2</code>")
                                .arg(p.local, p.upstream));
    meta->setTextInteractionFlags(Qt::TextSelectableByMouse);
    meta->setWordWrap(true);
    dl->addWidget(meta);

    auto* splitter = new QSplitter(Qt::Horizontal);

    auto* local_box = new QPlainTextEdit;
    local_box->setReadOnly(true);
    local_box->setPlainText(local_text);
    local_box->setStyleSheet(QStringLiteral("font-family: monospace;"));

    auto* up_box = new QPlainTextEdit;
    up_box->setReadOnly(true);
    up_box->setPlainText(up_text);
    up_box->setStyleSheet(QStringLiteral("font-family: monospace;"));

    splitter->addWidget(local_box);
    splitter->addWidget(up_box);
    splitter->setSizes({500, 500});
    dl->addWidget(splitter, 1);

    auto* close_btn = new QPushButton(QStringLiteral("Close"));
    connect(close_btn, &QPushButton::clicked, &dlg, &QDialog::accept);
    auto* br = new QHBoxLayout;
    br->addStretch();
    br->addWidget(close_btn);
    dl->addLayout(br);

    dlg.exec();
}

void SkillDiffsSection::on_accept_upstream() {
    const auto sel = table_->selectionModel()->selectedRows();
    if (sel.isEmpty())
        return;
    const int row = sel.first().row();
    Paths p = paths_for_row(row);
    if (p.local.isEmpty() || p.upstream.isEmpty()) {
        show_status(QStringLiteral("Accept unavailable — missing local or upstream path"), true);
        return;
    }

    const auto choice = QMessageBox::question(
        this, QStringLiteral("Confirm accept upstream"),
        QStringLiteral("Overwrite\n  <b>%1</b>\nwith\n  <b>%2</b>?\n\n"
                       "Recover via <code>git checkout</code> if needed.")
            .arg(p.local, p.upstream),
        QMessageBox::Yes | QMessageBox::No);
    if (choice != QMessageBox::Yes)
        return;

    // Atomic write: <local>.tmp + rename, mirrors the Track 7C
    // propose-fix safety pattern.
    QFile up(p.upstream);
    if (!up.open(QIODevice::ReadOnly)) {
        show_status(QStringLiteral("Read failed: %1").arg(up.errorString()), true);
        return;
    }
    const QByteArray bytes = up.readAll();
    up.close();

    const QString tmp_path = p.local + QStringLiteral(".tmp");
    QFile tmp(tmp_path);
    if (!tmp.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        show_status(QStringLiteral("Write failed (open .tmp): %1").arg(tmp.errorString()), true);
        return;
    }
    if (tmp.write(bytes) != bytes.size() || !tmp.flush()) {
        const QString err = tmp.errorString();
        tmp.close();
        QFile::remove(tmp_path);
        show_status(QStringLiteral("Write failed (write/flush): %1").arg(err), true);
        return;
    }
    tmp.close();
    QFile::remove(p.local);
    if (!QFile::rename(tmp_path, p.local)) {
        QFile::remove(tmp_path);
        show_status(QStringLiteral("Write failed (rename): could not move .tmp into place"), true);
        return;
    }
    show_status(QStringLiteral("Accepted upstream → %1").arg(p.local));
    table_->removeRow(row);
}

void SkillDiffsSection::show_status(const QString& msg, bool error) {
    status_lbl_->setText(msg);
    status_lbl_->setStyleSheet(error ? QStringLiteral("color: #c33;")
                                     : QStringLiteral("color: #393;"));
}

} // namespace fincept::screens
