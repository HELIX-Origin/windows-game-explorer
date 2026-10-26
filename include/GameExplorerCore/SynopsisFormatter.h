#pragma once

#include <string>
#include <vector>

namespace GameExplorer {

// A contiguous span of synopsis text sharing the same inline formatting.
struct SynopsisRun {
    std::wstring text;
    bool bold{ false };
    bool italic{ false };
    bool underline{ false };
    bool strike{ false };
    bool code{ false };
};

enum class SynopsisBlockKind {
    Heading1,
    Heading2,
    Heading3,
    Paragraph,
    Bullet,
    Numbered,
    Quote,
    Separator,
    CodeBlock,
    Table
};

// One rendered line/paragraph of a synopsis.
struct SynopsisBlock {
    SynopsisBlockKind kind{ SynopsisBlockKind::Paragraph };
    // GFM list marker as authored ("•" for plain bullets, "☐"/"☑" for task items, or the number
    // such as "1." for ordered items). Empty for every other block kind.
    std::wstring marker;
    std::vector<SynopsisRun> runs;
    // Table rows: the first row is the header row; each row is a list of cells, each cell a list
    // of formatted runs. Empty for every other block kind.
    std::vector<std::vector<std::vector<SynopsisRun>>> rows;
};

// Parses the GitHub Flavored Markdown (GFM) synopsis markup used everywhere a synopsis is shown:
//   "# " .. "###### "                 -> headings (levels 4-6 render at the level-3 size)
//   "- ", "* ", "+ "                  -> bullet list item
//   "- [ ] ", "- [x] "                -> task list item
//   "1. ", "1) "                      -> ordered list item
//   "> "                              -> block quote
//   "---", "***", "___"               -> horizontal rule
//   "```" / "~~~" fenced blocks       -> code block (contents are not formatted)
//   "| a | b |" + "| --- | --- |"     -> table (the header row is the first row)
//   **bold**, __bold__, *italic*, _italic_, ~~strike~~, `code`
//   [label](url), ![alt](src), <https://autolink>, bare https:// links, <u>/<br> inline HTML
//   "\"                               -> escapes the next markup character
// Every non-blank line becomes its own block, so hard line breaks and blank-line separated
// paragraphs both survive. Unbalanced markers apply to the rest of the line, so plain text
// (including storefront-supplied synopses) always renders unchanged.
[[nodiscard]] std::vector<SynopsisBlock> ParseSynopsis(const std::wstring& text);

// Removes synopsis markup, producing readable plain text while preserving structure.
[[nodiscard]] std::wstring StripSynopsisMarkup(const std::wstring& text);

} // namespace GameExplorer
