#pragma once

#include <QString>

namespace Acheron {
namespace Core {

// https://github.com/google/emoji-segmenter/blob/1cada87c62550446fca6a42a69743688b4539a4c/emoji_presentation_scanner.rl#L30-L45
// Values must match the Ragel-generated scanner's category numbering:
//   EMOJI=0, EMOJI_TEXT_PRESENTATION=1, EMOJI_EMOJI_PRESENTATION=2,
//   EMOJI_MODIFIER_BASE_TEXT=3, EMOJI_MODIFIER_BASE_EMOJI=4,
//   EMOJI_MODIFIER=5, ..., OTHER=17 (unused by scanner)
enum CharacterCategory {
    EMOJI = 0,
    EMOJI_TEXT_PRESENTATION = 1,
    EMOJI_EMOJI_PRESENTATION = 2,
    EMOJI_MODIFIER_BASE = 3,          // EMOJI_MODIFIER_BASE_TEXT in Ragel
    EMOJI_MODIFIER_BASE_EMOJI = 4,    // EMOJI_MODIFIER_BASE_EMOJI in Ragel
    EMOJI_MODIFIER = 5,
    EMOJI_VS_BASE = 6,
    REGIONAL_INDICATOR = 7,
    KEYCAP_BASE = 8,
    COMBINING_ENCLOSING_KEYCAP = 9,
    COMBINING_ENCLOSING_CIRCLE_BACKSLASH = 10,
    ZWJ = 11,
    VS15 = 12,
    VS16 = 13,
    TAG_BASE = 14,
    TAG_SEQUENCE = 15,
    TAG_TERM = 16,
    OTHER = 17
};

typedef CharacterCategory *emoji_text_iter_t;

// Count the number of emoji presentation sequences in a text string.
// Returns -1 if any non-emoji, non-whitespace content is found.
// Returns the emoji sequence count (0 if empty/all whitespace).
int countUnicodeEmojisSegmented(const QString &text);

} // namespace Core
} // namespace Acheron
