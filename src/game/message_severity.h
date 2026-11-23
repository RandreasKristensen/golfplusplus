#pragma once
// How a message reads to the player: a status (progress, a rules consequence, a
// hint) or an error (an action that was refused or failed). The producer says
// which; nothing infers it from the wording, a text key's prefix or whether the
// server sent it. Drawn with the `error` text style, border and background only
// for an error.

enum class message_severity : unsigned char { status, error };
