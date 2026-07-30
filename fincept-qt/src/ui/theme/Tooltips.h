// src/ui/theme/Tooltips.h
#pragma once

class QApplication;

namespace fincept::ui::tooltips {

/// Make every tooltip in the app behave the same way: appear the moment the
/// pointer lands, next to the pointer, and wrapped into a readable box.
///
/// Out of the box Qt does none of these consistently. It waits most of a
/// second before showing anything, and — the part that actually hurts — it
/// word-wraps a tooltip ONLY when the text happens to be rich text, because
/// QTipLabel decides by asking Qt::mightBeRichText(). So a tooltip written as
/// "<b>x</b> …" wraps into a tidy box while the identical sentence written as
/// plain text is laid out on a single line the width of the paragraph. On a
/// wide monitor that line is also too wide to sit under the cursor, so Qt
/// shoves it against a screen edge — which is why the unreadable ones also
/// appear nowhere near what the user is pointing at. Both symptoms have the
/// same cause and the same fix.
///
/// Call once, immediately after constructing the QApplication and before any
/// stylesheet is applied.
void install(QApplication& app);

} // namespace fincept::ui::tooltips
