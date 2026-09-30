#include "ui/ResponseParser.h"

#include <cassert>
#include <iostream>

int main()
{
    using namespace rose::ui;
    const ResponseDocument response = parseResponseDocument(
        "## Timeline\n\n"
        "| Date | Action | Basis |\n"
        "|:---|---|---:|\n"
        "| 2025-06-19 | Retired | 10 U.S.C. § 1552 |\n"
        "| 2026-09-11 | BCNR decision | Record |\n\n"
        "```cpp\nconst char* literal = \"a|b\";\n```\n");

    assert(response.blocks.size() == 3);
    assert(response.blocks[0].kind == ResponseBlockKind::Heading);
    const ResponseBlock& table = response.blocks[1];
    assert(table.kind == ResponseBlockKind::Table);
    assert(table.tableRows.size() == 3);
    assert(table.tableRows[0].size() == 3);
    assert(table.tableRows[1][1] == "Retired");
    assert(response.blocks[2].kind == ResponseBlockKind::CodeBlock);
    assert(response.blocks[2].text.find("a|b") != std::string::npos);

    const ResponseDocument escaped = parseResponseDocument(
        "| A | B |\n|---|---|\n| a\\|b | c |\n");
    assert(escaped.blocks.size() == 1);
    assert(escaped.blocks[0].tableRows[1][0] == "a|b");

    std::cout << "Rose response table parser: PASS\n";
}
