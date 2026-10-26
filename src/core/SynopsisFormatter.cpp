#include "GameExplorerCore/SynopsisFormatter.h"

#include <cwctype>

namespace GameExplorer {

namespace {

bool IsWordChar(wchar_t ch) {
    return std::iswalnum(static_cast<wint_t>(ch)) != 0;
}

bool IsSpace(wchar_t ch) {
    return ch == L' ' || ch == L'\t';
}

std::vector<std::wstring> SplitLines(const std::wstring& text) {
    std::vector<std::wstring> lines;
    std::wstring current;
    for (size_t i = 0; i < text.size(); ++i) {
        const wchar_t ch = text[i];
        if (ch == L'\r' || ch == L'\n') {
            if (ch == L'\r' && i + 1 < text.size() && text[i + 1] == L'\n') {
                ++i;
            }
            lines.push_back(current);
            current.clear();
            continue;
        }
        current.push_back(ch);
    }
    lines.push_back(current);

    for (auto& line : lines) {
        while (!line.empty() && (line.back() == L' ' || line.back() == L'\t')) {
            line.pop_back();
        }
    }
    return lines;
}

std::wstring TrimLeadingSpaces(const std::wstring& line) {
    size_t start = 0;
    while (start < line.size() && IsSpace(line[start])) {
        ++start;
    }
    return line.substr(start);
}

std::wstring TrimSpaces(const std::wstring& text) {
    const std::wstring leading = TrimLeadingSpaces(text);
    size_t end = leading.size();
    while (end > 0 && IsSpace(leading[end - 1])) {
        --end;
    }
    return leading.substr(0, end);
}

bool IsFenceStart(const std::wstring& trimmed, std::wstring& marker) {
    if (trimmed.rfind(L"```", 0) == 0) {
        marker = L"```";
        return true;
    }
    if (trimmed.rfind(L"~~~", 0) == 0) {
        marker = L"~~~";
        return true;
    }
    return false;
}

// A rule is three or more of the same marker character with nothing else but spaces.
bool IsHorizontalRule(const std::wstring& trimmed) {
    if (trimmed.size() < 3) return false;
    const wchar_t marker = trimmed.front();
    if (marker != L'-' && marker != L'*' && marker != L'_') return false;

    size_t count = 0;
    for (const wchar_t ch : trimmed) {
        if (ch == marker) {
            ++count;
        } else if (!IsSpace(ch)) {
            return false;
        }
    }
    return count >= 3;
}

// Returns the ATX heading level (1-6) or 0 when the line is not a heading.
int HeadingLevel(const std::wstring& trimmed) {
    size_t level = 0;
    while (level < trimmed.size() && trimmed[level] == L'#') {
        ++level;
    }
    if (level == 0 || level > 6) return 0;
    if (level >= trimmed.size() || !IsSpace(trimmed[level])) return 0;
    return static_cast<int>(level);
}

bool IsBulletMarker(const std::wstring& trimmed) {
    if (trimmed.size() < 2) return false;
    const wchar_t first = trimmed[0];
    if (first != L'-' && first != L'*' && first != L'+' && first != L'\u2022') return false;
    return IsSpace(trimmed[1]);
}

// Detects "1. " / "1) " ordered-list markers. Returns the content offset (after the space).
size_t OrderedMarkerLength(const std::wstring& trimmed) {
    size_t digits = 0;
    while (digits < trimmed.size() && std::iswdigit(static_cast<wint_t>(trimmed[digits])) != 0) {
        ++digits;
    }
    if (digits == 0 || digits > 9) return 0;
    if (digits + 1 >= trimmed.size()) return 0;
    if (trimmed[digits] != L'.' && trimmed[digits] != L')') return 0;
    if (!IsSpace(trimmed[digits + 1])) return 0;
    return digits + 2;
}

bool IsTableRow(const std::wstring& trimmed) {
    return !trimmed.empty() && trimmed.front() == L'|' && trimmed.find(L'|', 1) != std::wstring::npos;
}

bool IsTableDelimiter(const std::wstring& trimmed) {
    if (!IsTableRow(trimmed)) return false;
    bool hasDash = false;
    for (const wchar_t ch : trimmed) {
        if (ch == L'-') {
            hasDash = true;
        } else if (ch != L'|' && ch != L':' && !IsSpace(ch)) {
            return false;
        }
    }
    return hasDash;
}

std::vector<std::wstring> SplitTableCells(const std::wstring& trimmed) {
    std::vector<std::wstring> cells;
    std::wstring current;
    size_t i = 0;
    if (!trimmed.empty() && trimmed.front() == L'|') {
        ++i;
    }
    for (; i < trimmed.size(); ++i) {
        const wchar_t ch = trimmed[i];
        if (ch == L'\\' && i + 1 < trimmed.size() && trimmed[i + 1] == L'|') {
            current.push_back(L'|');
            ++i;
            continue;
        }
        if (ch == L'|') {
            cells.push_back(TrimSpaces(current));
            current.clear();
            continue;
        }
        current.push_back(ch);
    }
    const std::wstring tail = TrimSpaces(current);
    if (!tail.empty()) {
        cells.push_back(tail);
    }
    return cells;
}

struct RunStyle {
    bool bold{ false };
    bool italic{ false };
    bool underline{ false };
    bool strike{ false };
    bool code{ false };
};

void AppendRun(std::vector<SynopsisRun>& runs, const std::wstring& buffer, const RunStyle& style) {
    if (buffer.empty()) return;
    // Merge adjacent runs that share formatting so the renderer emits fewer inline elements.
    const bool mergeable = !runs.empty() &&
        runs.back().bold == style.bold &&
        runs.back().italic == style.italic &&
        runs.back().underline == style.underline &&
        runs.back().strike == style.strike &&
        runs.back().code == style.code;
    if (mergeable) {
        runs.back().text += buffer;
    } else {
        SynopsisRun run;
        run.text = buffer;
        run.bold = style.bold;
        run.italic = style.italic;
        run.underline = style.underline;
        run.strike = style.strike;
        run.code = style.code;
        runs.push_back(std::move(run));
    }
}

// Trims trailing punctuation that GFM does not treat as part of a bare autolink.
void TrimTrailingUrlPunctuation(std::wstring& url) {
    while (!url.empty() && (url.back() == L'.' || url.back() == L',' || url.back() == L';' ||
                            url.back() == L':' || url.back() == L'!' || url.back() == L'?')) {
        url.pop_back();
    }
    if (!url.empty() && url.back() == L')' && url.find(L'(') == std::wstring::npos) {
        url.pop_back();
    }
}

// Appends inline formatting for one block of GFM text.
void ParseInline(const std::wstring& content, std::vector<SynopsisRun>& runs) {
    std::wstring buffer;
    RunStyle style;

    auto flush = [&]() {
        AppendRun(runs, buffer, style);
        buffer.clear();
    };

    for (size_t i = 0; i < content.size();) {
        const wchar_t ch = content[i];

        if (ch == L'\\' && i + 1 < content.size()) {
            buffer.push_back(content[i + 1]);
            i += 2;
            continue;
        }

        if (ch == L'`') {
            const size_t close = content.find(L'`', i + 1);
            if (close != std::wstring::npos) {
                flush();
                RunStyle codeStyle = style;
                codeStyle.code = true;
                AppendRun(runs, content.substr(i + 1, close - i - 1), codeStyle);
                i = close + 1;
                continue;
            }
        }

        // Image markup is deliberately unsupported: artwork has its own slots, so "![alt](src)"
        // is kept as literal text instead of being silently turned into a link.
        if (ch == L'!' && i + 1 < content.size() && content[i + 1] == L'[') {
            const size_t labelEnd = content.find(L"](", i + 2);
            if (labelEnd != std::wstring::npos) {
                const size_t urlEnd = content.find(L')', labelEnd + 2);
                if (urlEnd != std::wstring::npos) {
                    buffer.append(content, i, urlEnd - i + 1);
                    i = urlEnd + 1;
                    continue;
                }
            }
        }

        if (ch == L'[') {
            const size_t labelEnd = content.find(L"](", i + 1);
            if (labelEnd != std::wstring::npos) {
                const size_t urlEnd = content.find(L')', labelEnd + 2);
                if (urlEnd != std::wstring::npos) {
                    flush();
                    // Render the human-readable label; the destination is not shown in the pane.
                    RunStyle linkStyle = style;
                    linkStyle.underline = true;
                    AppendRun(runs, content.substr(i + 1, labelEnd - i - 1), linkStyle);
                    i = urlEnd + 1;
                    continue;
                }
            }
        }

        if (ch == L'<') {
            const size_t tagEnd = content.find(L'>', i + 1);
            if (tagEnd != std::wstring::npos && tagEnd - i <= 64) {
                std::wstring tag = content.substr(i + 1, tagEnd - i - 1);
                for (auto& c : tag) {
                    c = static_cast<wchar_t>(std::towlower(static_cast<wint_t>(c)));
                }

                if (tag.rfind(L"http://", 0) == 0 || tag.rfind(L"https://", 0) == 0) {
                    flush();
                    RunStyle linkStyle = style;
                    linkStyle.underline = true;
                    AppendRun(runs, tag, linkStyle);
                    i = tagEnd + 1;
                    continue;
                }

                bool handled = true;
                if (tag == L"br" || tag == L"br/") {
                    buffer.push_back(L'\n');
                } else if (tag == L"u" || tag == L"ins") {
                    flush();
                    style.underline = true;
                } else if (tag == L"/u" || tag == L"/ins") {
                    flush();
                    style.underline = false;
                } else if (tag == L"b" || tag == L"strong") {
                    flush();
                    style.bold = true;
                } else if (tag == L"/b" || tag == L"/strong") {
                    flush();
                    style.bold = false;
                } else if (tag == L"i" || tag == L"em") {
                    flush();
                    style.italic = true;
                } else if (tag == L"/i" || tag == L"/em") {
                    flush();
                    style.italic = false;
                } else if (tag == L"s" || tag == L"del" || tag == L"strike") {
                    flush();
                    style.strike = true;
                } else if (tag == L"/s" || tag == L"/del" || tag == L"/strike") {
                    flush();
                    style.strike = false;
                } else {
                    handled = false;
                }

                if (handled) {
                    i = tagEnd + 1;
                    continue;
                }
            }
        }

        // GFM extended autolinks: bare http(s) URLs at a word boundary.
        if (ch == L'h' && (i == 0 || !IsWordChar(content[i - 1])) &&
            (content.compare(i, 7, L"http://") == 0 || content.compare(i, 8, L"https://") == 0)) {
            size_t end = i;
            while (end < content.size() && !IsSpace(content[end]) && content[end] != L'<' &&
                   content[end] != L'>') {
                ++end;
            }
            std::wstring url = content.substr(i, end - i);
            TrimTrailingUrlPunctuation(url);
            if (url.size() > 8) {
                flush();
                RunStyle linkStyle = style;
                linkStyle.underline = true;
                AppendRun(runs, url, linkStyle);
                i += url.size();
                continue;
            }
        }

        if (ch == L'~' && i + 1 < content.size() && content[i + 1] == L'~') {
            flush();
            style.strike = !style.strike;
            i += 2;
            continue;
        }

        if (ch == L'*' && i + 1 < content.size() && content[i + 1] == L'*') {
            flush();
            style.bold = !style.bold;
            i += 2;
            continue;
        }

        if (ch == L'_' && i + 1 < content.size() && content[i + 1] == L'_') {
            flush();
            style.bold = !style.bold;
            i += 2;
            continue;
        }

        if (ch == L'*') {
            flush();
            style.italic = !style.italic;
            ++i;
            continue;
        }

        if (ch == L'_') {
            const bool canClose = style.italic;
            const bool canOpen = (i == 0 || !IsWordChar(content[i - 1])) &&
                                 i + 1 < content.size() && !std::iswspace(static_cast<wint_t>(content[i + 1]));
            if (canClose || canOpen) {
                flush();
                style.italic = !style.italic;
                ++i;
                continue;
            }
        }

        buffer.push_back(ch);
        ++i;
    }

    flush();
}

std::vector<std::vector<SynopsisRun>> ParseTableRow(const std::wstring& trimmed) {
    std::vector<std::vector<SynopsisRun>> row;
    for (const auto& cell : SplitTableCells(trimmed)) {
        std::vector<SynopsisRun> runs;
        ParseInline(cell, runs);
        row.push_back(std::move(runs));
    }
    return row;
}

} // namespace

std::vector<SynopsisBlock> ParseSynopsis(const std::wstring& text) {
    std::vector<SynopsisBlock> blocks;
    if (text.empty()) return blocks;

    std::vector<std::wstring> codeLines;
    auto flushCode = [&blocks, &codeLines]() {
        if (codeLines.empty()) return;
        SynopsisBlock block;
        block.kind = SynopsisBlockKind::CodeBlock;
        SynopsisRun run;
        run.code = true;
        for (size_t i = 0; i < codeLines.size(); ++i) {
            if (i != 0) run.text.push_back(L'\n');
            run.text += codeLines[i];
        }
        block.runs.push_back(std::move(run));
        blocks.push_back(std::move(block));
        codeLines.clear();
    };

    const std::vector<std::wstring> lines = SplitLines(text);
    bool inFence = false;
    std::wstring fenceMarker;

    for (size_t li = 0; li < lines.size(); ++li) {
        const std::wstring& rawLine = lines[li];
        const std::wstring trimmed = TrimLeadingSpaces(rawLine);

        if (inFence) {
            if (trimmed.rfind(fenceMarker, 0) == 0) {
                flushCode();
                inFence = false;
                fenceMarker.clear();
            } else {
                codeLines.push_back(rawLine);
            }
            continue;
        }
        if (IsFenceStart(trimmed, fenceMarker)) {
            inFence = true;
            codeLines.clear();
            continue;
        }
        if (trimmed.empty()) continue;

        if (IsHorizontalRule(trimmed)) {
            SynopsisBlock rule;
            rule.kind = SynopsisBlockKind::Separator;
            blocks.push_back(std::move(rule));
            continue;
        }

        // GFM table: a header row followed by a delimiter row, then body rows.
        if (IsTableRow(trimmed) && li + 1 < lines.size() &&
            IsTableDelimiter(TrimLeadingSpaces(lines[li + 1]))) {
            SynopsisBlock table;
            table.kind = SynopsisBlockKind::Table;
            table.rows.push_back(ParseTableRow(trimmed));
            li += 2;
            while (li < lines.size()) {
                const std::wstring row = TrimLeadingSpaces(lines[li]);
                if (!IsTableRow(row) || IsTableDelimiter(row)) break;
                table.rows.push_back(ParseTableRow(row));
                ++li;
            }
            --li;
            blocks.push_back(std::move(table));
            continue;
        }

        SynopsisBlock block;
        std::wstring content;

        const int heading = HeadingLevel(trimmed);
        if (heading == 1) {
            block.kind = SynopsisBlockKind::Heading1;
            content = trimmed.substr(2);
        } else if (heading == 2) {
            block.kind = SynopsisBlockKind::Heading2;
            content = trimmed.substr(3);
        } else if (heading >= 3) {
            block.kind = SynopsisBlockKind::Heading3;
            content = trimmed.substr(static_cast<size_t>(heading) + 1);
        } else if (IsBulletMarker(trimmed)) {
            block.kind = SynopsisBlockKind::Bullet;
            content = trimmed.substr(2);
            block.marker = L"\u2022";
            if (content.size() >= 4 && content[0] == L'[' && content[2] == L']' && IsSpace(content[3])) {
                if (content[1] == L' ' || content[1] == L'\t') {
                    block.marker = L"\u2610";
                    content = content.substr(4);
                } else if (content[1] == L'x' || content[1] == L'X') {
                    block.marker = L"\u2611";
                    content = content.substr(4);
                }
            }
        } else if (const size_t ordered = OrderedMarkerLength(trimmed); ordered > 0) {
            block.kind = SynopsisBlockKind::Numbered;
            block.marker = trimmed.substr(0, ordered - 1);
            content = trimmed.substr(ordered);
        } else if (trimmed.front() == L'>') {
            block.kind = SynopsisBlockKind::Quote;
            content = trimmed.substr(1);
            if (!content.empty() && IsSpace(content.front())) {
                content.erase(0, 1);
            }
        } else {
            block.kind = SynopsisBlockKind::Paragraph;
            content = trimmed;
        }

        ParseInline(content, block.runs);
        if (!block.runs.empty()) {
            blocks.push_back(std::move(block));
        }
    }
    flushCode();

    return blocks;
}

std::wstring StripSynopsisMarkup(const std::wstring& text) {
    std::wstring out;
    for (const auto& block : ParseSynopsis(text)) {
        if (!out.empty()) out.push_back(L'\n');

        if (block.kind == SynopsisBlockKind::Table) {
            for (size_t r = 0; r < block.rows.size(); ++r) {
                if (r != 0) out.push_back(L'\n');
                const auto& row = block.rows[r];
                for (size_t c = 0; c < row.size(); ++c) {
                    if (c != 0) out += L" | ";
                    for (const auto& run : row[c]) {
                        out += run.text;
                    }
                }
            }
            continue;
        }

        switch (block.kind) {
        case SynopsisBlockKind::Bullet:
        case SynopsisBlockKind::Numbered:
            out += block.marker;
            out += L' ';
            break;
        case SynopsisBlockKind::Quote:
            out += L"> ";
            break;
        case SynopsisBlockKind::Separator:
            out += L"---";
            break;
        default:
            break;
        }

        for (const auto& run : block.runs) {
            out += run.text;
        }
    }
    return out;
}

} // namespace GameExplorer
