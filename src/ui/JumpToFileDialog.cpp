// SPDX-License-Identifier: MIT
#include "ui/JumpToFileDialog.hpp"

#include <algorithm>
#include <cctype>

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

namespace koalaamp::ui {

namespace {

std::string lowered(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

}  // namespace

int jumpMatchScore(std::string_view pattern, std::string_view text) {
    if (pattern.empty()) {
        return 1;
    }
    const std::string needle = lowered(pattern);
    const std::string haystack = lowered(text);

    const std::size_t substring = haystack.find(needle);
    if (substring != std::string::npos) {
        return 1000 - static_cast<int>(std::min<std::size_t>(substring, 900));
    }

    // Subsequence: every pattern character in order. Reward density.
    std::size_t cursor = 0;
    std::size_t first = std::string::npos;
    for (std::size_t i = 0; i < haystack.size() && cursor < needle.size(); ++i) {
        if (haystack[i] == needle[cursor]) {
            if (cursor == 0) {
                first = i;
            }
            ++cursor;
        }
    }
    if (cursor != needle.size()) {
        return 0;
    }
    const std::size_t span = haystack.size() - first;
    return 500 - static_cast<int>(std::min<std::size_t>(span, 400));
}

std::vector<std::size_t> rankJumpMatches(const std::vector<PleditEntry>& entries,
                                         std::string_view pattern) {
    std::vector<std::pair<int, std::size_t>> scored;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const int score = jumpMatchScore(pattern, entries[i].title);
        if (score > 0) {
            scored.emplace_back(score, i);
        }
    }
    std::stable_sort(scored.begin(), scored.end(),
                     [](const auto& a, const auto& b) { return a.first > b.first; });
    std::vector<std::size_t> out;
    out.reserve(scored.size());
    for (const auto& [score, index] : scored) {
        (void)score;
        out.push_back(index);
    }
    return out;
}

JumpToFileDialog::JumpToFileDialog(const PleditState& state, QWidget* parent)
    : QDialog(parent), m_state(state) {
    setWindowTitle(QStringLiteral("Jump to file"));
    auto* layout = new QVBoxLayout(this);

    m_filter = new QLineEdit(this);
    m_filter->setPlaceholderText(QStringLiteral("Jump to file..."));
    layout->addWidget(m_filter);

    m_list = new QListWidget(this);
    layout->addWidget(m_list);

    auto* buttons = new QHBoxLayout();
    m_jump = new QPushButton(QStringLiteral("Jump"), this);
    m_enqueue = new QPushButton(QStringLiteral("Enqueue"), this);
    m_clear = new QPushButton(QStringLiteral("Clear queue"), this);
    buttons->addWidget(m_jump);
    buttons->addWidget(m_enqueue);
    buttons->addWidget(m_clear);
    layout->addLayout(buttons);

    connect(m_filter, &QLineEdit::textChanged, this,
            [this](const QString& text) { setFilter(text); });
    connect(m_list, &QListWidget::itemDoubleClicked, this, [this] { activateCurrent(); });
    connect(m_jump, &QPushButton::clicked, this, [this] { activateCurrent(); });
    connect(m_enqueue, &QPushButton::clicked, this,
            [this] { enqueueMatch(m_list->currentRow()); });
    connect(m_clear, &QPushButton::clicked, this, [this] { requestClearQueue(); });

    refresh();
}

void JumpToFileDialog::setState(const PleditState& state) {
    m_state = state;
    refresh();
}

void JumpToFileDialog::setFilter(const QString& pattern) {
    m_matches = rankJumpMatches(m_state.entries, pattern.toStdString());
    m_list->clear();
    for (const std::size_t index : m_matches) {
        const QString label = QStringLiteral("%1. %2")
                                  .arg(index + 1)
                                  .arg(QString::fromStdString(m_state.entries[index].title));
        const QString queue = queueLabel(index);
        m_list->addItem(queue.isEmpty() ? label
                                        : label + QStringLiteral("  [") + queue +
                                              QStringLiteral("]"));
    }
    if (!m_matches.empty()) {
        m_list->setCurrentRow(0);
    }
}

void JumpToFileDialog::refresh() {
    setFilter(m_filter != nullptr ? m_filter->text() : QString());
}

QString JumpToFileDialog::queueLabel(std::size_t index) const {
    const std::size_t position = queuePosition(m_state, index);
    return position == 0 ? QString() : QString::number(position);
}

void JumpToFileDialog::activateCurrent() { activateMatch(m_list->currentRow()); }

void JumpToFileDialog::activateMatch(int listRow) {
    if (listRow < 0 || listRow >= static_cast<int>(m_matches.size())) {
        return;
    }
    emit jumpRequested(static_cast<int>(m_matches[static_cast<std::size_t>(listRow)]));
    accept();
}

void JumpToFileDialog::enqueueMatch(int listRow) {
    if (listRow < 0 || listRow >= static_cast<int>(m_matches.size())) {
        return;
    }
    emit enqueueRequested(static_cast<int>(m_matches[static_cast<std::size_t>(listRow)]));
}

void JumpToFileDialog::requestClearQueue() { emit clearQueueRequested(); }

}  // namespace koalaamp::ui
