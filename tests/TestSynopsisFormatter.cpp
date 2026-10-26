#include "TestHarness.h"
#include "GameExplorerCore/SynopsisFormatter.h"

#include <string>

using namespace GameExplorer;
using namespace TestFramework;

namespace {

std::wstring RunsToText(const SynopsisBlock& block) {
    std::wstring out;
    for (const auto& run : block.runs) out += run.text;
    return out;
}

bool HasFormatting(const SynopsisBlock& block, bool SynopsisRun::*member) {
    for (const auto& run : block.runs) {
        if (run.*member) return true;
    }
    return false;
}

} // namespace

TEST_CASE(SynopsisFormatterPlainTextPassThrough) {
    const std::wstring plain = L"Take part in an epic and ever-changing FINAL FANTASY.";
    const auto blocks = ParseSynopsis(plain);
    ASSERT_TRUE(blocks.size() == 1);
    ASSERT_TRUE(blocks[0].kind == SynopsisBlockKind::Paragraph);
    ASSERT_TRUE(RunsToText(blocks[0]) == plain);
}

TEST_CASE(SynopsisFormatterHeadingsAndBullets) {
    const auto blocks = ParseSynopsis(
        L"# Overview\r\n"
        L"## Features\r\n"
        L"- Crafting\r\n"
        L"* Raiding\r\n"
        L"### Notes\r\n");
    ASSERT_TRUE(blocks.size() == 5);
    ASSERT_TRUE(blocks[0].kind == SynopsisBlockKind::Heading1);
    ASSERT_TRUE(RunsToText(blocks[0]) == L"Overview");
    ASSERT_TRUE(blocks[1].kind == SynopsisBlockKind::Heading2);
    ASSERT_TRUE(RunsToText(blocks[1]) == L"Features");
    ASSERT_TRUE(blocks[2].kind == SynopsisBlockKind::Bullet);
    ASSERT_TRUE(RunsToText(blocks[2]) == L"Crafting");
    ASSERT_TRUE(blocks[3].kind == SynopsisBlockKind::Bullet);
    ASSERT_TRUE(RunsToText(blocks[3]) == L"Raiding");
    ASSERT_TRUE(blocks[4].kind == SynopsisBlockKind::Heading3);
}

TEST_CASE(SynopsisFormatterInlineEmphasis) {
    const auto blocks = ParseSynopsis(
        L"A **bold** and *italic* and __strong__ and <u>underlined</u> and ~~gone~~ end.");
    ASSERT_TRUE(blocks.size() == 1);
    const auto& runs = blocks[0].runs;
    ASSERT_TRUE(runs.size() == 11);
    ASSERT_TRUE(runs[0].text == L"A ");
    ASSERT_TRUE(runs[1].text == L"bold");
    ASSERT_TRUE(runs[1].bold);
    ASSERT_TRUE(runs[2].text == L" and ");
    ASSERT_TRUE(runs[3].text == L"italic");
    ASSERT_TRUE(runs[3].italic);
    ASSERT_TRUE(runs[4].text == L" and ");
    ASSERT_TRUE(runs[5].text == L"strong");
    ASSERT_TRUE(runs[5].bold);
    ASSERT_TRUE(runs[6].text == L" and ");
    ASSERT_TRUE(runs[7].text == L"underlined");
    ASSERT_TRUE(runs[7].underline);
    ASSERT_TRUE(runs[8].text == L" and ");
    ASSERT_TRUE(runs[9].text == L"gone");
    ASSERT_TRUE(runs[9].strike);
    ASSERT_TRUE(runs[10].text == L" end.");
}

TEST_CASE(SynopsisFormatterPreservesUnderscoreWordsAndEscapes) {
    const auto blocks = ParseSynopsis(L"FINAL_FANTASY_xiv and \\*literal\\* stars");
    ASSERT_TRUE(blocks.size() == 1);
    ASSERT_TRUE(RunsToText(blocks[0]) == L"FINAL_FANTASY_xiv and *literal* stars");
    ASSERT_TRUE(!HasFormatting(blocks[0], &SynopsisRun::italic));

    const auto underscored = ParseSynopsis(L"_styled_ text");
    ASSERT_TRUE(underscored.size() == 1);
    ASSERT_TRUE(underscored[0].runs[0].text == L"styled");
    ASSERT_TRUE(underscored[0].runs[0].italic);
}

TEST_CASE(SynopsisFormatterStripsMarkup) {
    const std::wstring stripped = StripSynopsisMarkup(
        L"# Title\n\nIntro with **bold** text.\n- First\n- Second");
    ASSERT_TRUE(stripped == L"Title\nIntro with bold text.\n\u2022 First\n\u2022 Second");
}

TEST_CASE(SynopsisFormatterPreservesLineBreaks) {
    const auto blocks = ParseSynopsis(L"First line\r\nSecond line\n\nThird paragraph");
    ASSERT_TRUE(blocks.size() == 3);
    ASSERT_TRUE(RunsToText(blocks[0]) == L"First line");
    ASSERT_TRUE(RunsToText(blocks[1]) == L"Second line");
    ASSERT_TRUE(RunsToText(blocks[2]) == L"Third paragraph");
}

TEST_CASE(SynopsisFormatterNumberedQuotesAndRules) {
    const auto blocks = ParseSynopsis(
        L"1. Install the client\n"
        L"2) Launch the game\n"
        L"+ Optional step\n"
        L"- [ ] Unchecked task\n"
        L"- [x] Checked task\n"
        L"> A quoted line\n"
        L"---\n"
        L"After the rule");
    ASSERT_TRUE(blocks.size() == 8);
    ASSERT_TRUE(blocks[0].kind == SynopsisBlockKind::Numbered);
    ASSERT_TRUE(blocks[0].marker == L"1.");
    ASSERT_TRUE(RunsToText(blocks[0]) == L"Install the client");
    ASSERT_TRUE(blocks[1].kind == SynopsisBlockKind::Numbered);
    ASSERT_TRUE(blocks[1].marker == L"2)");
    ASSERT_TRUE(RunsToText(blocks[1]) == L"Launch the game");
    ASSERT_TRUE(blocks[2].kind == SynopsisBlockKind::Bullet);
    ASSERT_TRUE(RunsToText(blocks[2]) == L"Optional step");
    ASSERT_TRUE(blocks[3].kind == SynopsisBlockKind::Bullet);
    ASSERT_TRUE(blocks[3].marker == L"\u2610");
    ASSERT_TRUE(RunsToText(blocks[3]) == L"Unchecked task");
    ASSERT_TRUE(blocks[4].kind == SynopsisBlockKind::Bullet);
    ASSERT_TRUE(blocks[4].marker == L"\u2611");
    ASSERT_TRUE(blocks[5].kind == SynopsisBlockKind::Quote);
    ASSERT_TRUE(RunsToText(blocks[5]) == L"A quoted line");
    ASSERT_TRUE(blocks[6].kind == SynopsisBlockKind::Separator);
    ASSERT_TRUE(blocks[7].kind == SynopsisBlockKind::Paragraph);
    ASSERT_TRUE(RunsToText(blocks[7]) == L"After the rule");
}

TEST_CASE(SynopsisFormatterCodeAndLinks) {
    const auto inlineBlocks = ParseSynopsis(L"Run `setup.exe` first.");
    ASSERT_TRUE(inlineBlocks.size() == 1);
    ASSERT_TRUE(inlineBlocks[0].runs.size() == 3);
    ASSERT_TRUE(inlineBlocks[0].runs[1].text == L"setup.exe");
    ASSERT_TRUE(inlineBlocks[0].runs[1].code);

    const auto fenced = ParseSynopsis(L"```\nline one\nline two\n```\nDone");
    ASSERT_TRUE(fenced.size() == 2);
    ASSERT_TRUE(fenced[0].kind == SynopsisBlockKind::CodeBlock);
    ASSERT_TRUE(fenced[0].runs.size() == 1);
    ASSERT_TRUE(fenced[0].runs[0].text == L"line one\nline two");

    const auto link = ParseSynopsis(L"See [the manual](https://example.com).");
    ASSERT_TRUE(link.size() == 1);
    ASSERT_TRUE(link[0].runs.size() == 3);
    ASSERT_TRUE(link[0].runs[1].text == L"the manual");
    ASSERT_TRUE(link[0].runs[1].underline);

    const std::wstring stripped = StripSynopsisMarkup(L"> Quoted\n---\n1. Step");
    ASSERT_TRUE(stripped == L"> Quoted\n---\n1. Step");
}

TEST_CASE(SynopsisFormatterTablesAndAutolinks) {    const auto blocks = ParseSynopsis(
        L"| Name | Value |\n"
        L"| :--- | ---: |\n"
        L"| A | **B** |\n"
        L"\n"
        L"See https://example.com/docs.");
    ASSERT_TRUE(blocks.size() == 2);
    ASSERT_TRUE(blocks[0].kind == SynopsisBlockKind::Table);
    ASSERT_TRUE(blocks[0].rows.size() == 2);
    ASSERT_TRUE(blocks[0].rows[0].size() == 2);
    ASSERT_TRUE(blocks[0].rows[0][0][0].text == L"Name");
    ASSERT_TRUE(blocks[0].rows[1][0][0].text == L"A");
    ASSERT_TRUE(blocks[0].rows[1][1][0].text == L"B");
    ASSERT_TRUE(blocks[0].rows[1][1][0].bold);

    ASSERT_TRUE(blocks[1].kind == SynopsisBlockKind::Paragraph);
    ASSERT_TRUE(blocks[1].runs.size() == 3);
    ASSERT_TRUE(blocks[1].runs[1].text == L"https://example.com/docs");
    ASSERT_TRUE(blocks[1].runs[1].underline);
    ASSERT_TRUE(blocks[1].runs[2].text == L".");

    const auto stripped = ParseSynopsis(L"| a | b |\n| --- | --- |\n| 1 | 2 |");
    ASSERT_TRUE(StripSynopsisMarkup(L"| a | b |\n| --- | --- |\n| 1 | 2 |") == L"a | b\n1 | 2");
    ASSERT_TRUE(stripped.size() == 1);
}

TEST_CASE(SynopsisFormatterKeepsImageMarkupLiteral) {
    const std::wstring source = L"![cover](https://example.com/cover.png)";
    const auto blocks = ParseSynopsis(source);
    ASSERT_TRUE(blocks.size() == 1);
    ASSERT_TRUE(RunsToText(blocks[0]) == source);
    ASSERT_TRUE(!HasFormatting(blocks[0], &SynopsisRun::underline));
}
