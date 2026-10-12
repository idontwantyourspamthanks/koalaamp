// SPDX-License-Identifier: MIT
//
// JumpToFileDialog — the `J` "Jump to file" dialog (docs/spec.md §5.4): a fuzzy
// filter over the playlist entries with a queue column and the JTFE verbs
// (enqueue the highlighted match, clear the queue).
//
// The matching itself is a pure function (`rankJumpMatches`) so it is unit
// tested without a widget; the dialog only renders the ranking and forwards
// the verbs as signals. The classic skin's own `pledit.bmp` art does not cover
// a dialog this size, so this is a plain Qt dialog (skinning JTFE popups is out
// of scope); it is a list + line edit, laid out minimally.
#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include <QDialog>

#include "ui/PleditState.hpp"

class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;

namespace koalaamp::ui {

/// Fuzzy match score of `pattern` against `text`: 0 = no match, higher = better.
/// A case-insensitive substring scores above a subsequence; earlier and denser
/// matches score higher.
int jumpMatchScore(std::string_view pattern, std::string_view text);

/// Entry indices matching `pattern`, best first (ties keep playlist order).
/// An empty pattern returns every index.
std::vector<std::size_t> rankJumpMatches(const std::vector<PleditEntry>& entries,
                                         std::string_view pattern);

class JumpToFileDialog : public QDialog {
    Q_OBJECT

public:
    explicit JumpToFileDialog(const PleditState& state, QWidget* parent = nullptr);

    /// Refreshes the list from `state` (queue positions included).
    void setState(const PleditState& state);

    /// Indices currently listed, best first.
    const std::vector<std::size_t>& matches() const { return m_matches; }
    /// Queue column text for an entry ("1".."n" or empty).
    QString queueLabel(std::size_t index) const;

    /// Applies a filter (also driven by the line edit).
    void setFilter(const QString& pattern);

    /// Verb entry points (the same path the buttons take). `listRow` indexes
    /// `matches()`; out-of-range rows do nothing.
    void activateMatch(int listRow);
    void enqueueMatch(int listRow);
    void requestClearQueue();

signals:
    void jumpRequested(int index);     ///< play the highlighted entry
    void enqueueRequested(int index);  ///< enqueue the highlighted entry
    void clearQueueRequested();

private:
    void refresh();
    void activateCurrent();

    PleditState m_state;
    std::vector<std::size_t> m_matches;
    QLineEdit* m_filter = nullptr;
    QListWidget* m_list = nullptr;
    QPushButton* m_jump = nullptr;
    QPushButton* m_enqueue = nullptr;
    QPushButton* m_clear = nullptr;
};

}  // namespace koalaamp::ui
