#pragma once

// The module's memo of parsed content and built courses. It is the one piece
// of module-global mutable state AGENTS.md allows: a pure cache of the
// immutable embedded content, built on first use and rebuilt if the instance
// restarts. It never holds game state; that lives in the tables (lib.cpp).

#include "server_content.h"

#include <string>

// The parsed content, or nullptr when the embedded content is broken
// (content_error says why; init refuses to publish then).
const server_content* cached_content();
const std::string& content_error();

// The course, built on first use. nullptr when it is unknown or fails to build.
const server_course* cached_course(const std::string& course_id);
