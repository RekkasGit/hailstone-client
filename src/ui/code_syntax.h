#pragma once

// Lua syntax for the code editor (ui.CodeEditor, ui/lua/lua_code_editor.cpp): the spans of a text to colour.
// Comments and strings include Lua's long brackets ([[ ]], [==[ ]==], --[[ ]]); a name after '.' or ':' is a member
// (mq.TLO.Me.PctHPs); the names the Lua socials and scripts use most (mq, state, button, print, ...) are built-ins.

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace nmsc::ui {

enum class CodeToken : std::uint8_t { Name, Keyword, String, Comment, Number, Builtin, Member, Symbol };

struct CodeSpan {
    std::size_t begin = 0, end = 0; // bytes [begin, end)
    CodeToken kind = CodeToken::Name;
};

// Every span that is not white space, in order.
std::vector<CodeSpan> lua_code_spans(std::string_view text);

} // namespace nmsc::ui
