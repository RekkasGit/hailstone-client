#pragma once

// MacroQuest's arithmetic (its Calculate, FastCalculate and EvaluateRPN in MQ2Utilities.cpp, used as the
// specification): the text behind ${Math.Calc[...]}, the other Math members and ${If[...]}'s condition. Written for
// this client from how MQ behaves, not copied from it. The same text gives the same number, quirks included:
//  - NULL, TRUE and FALSE (any case) are 0, 1 and 0; any other letter, ',' or a lone '=' is an error (nullopt);
//  - spaces are skipped everywhere, so "1 2" is 12; a number is its longest leading decimal ("1.2.3" is 1.2, "." 0);
//  - operators, loosest first: || ; && ; | ; ^^ (xor) ; & ; == != ; < <= > >= ; << >> ; + - ; * / \ (whole-number
//    division) % ; ^ (power) ; unary - ! ~. All of them group from the left, the unary ones too, so -2^2 is 4 and
//    2^3^2 is 64;
//  - "++" and "--" are both an addition; '-' is unary unless a number or ')' is before it;
//  - \, %, &, |, ^^, <<, >>, ! and ~ work on 32-bit whole numbers (a value cut toward zero; one outside 32 bits or
//    NaN becomes -2147483648, as x86's conversion gives it); the rest on doubles;
//  - / or \ or % by zero, a ')' without its '(', or an operator short of its operand at the bottom of the stack is
//    an error. A '(' never closed is ignored, and a binary operator short of its left operand takes 0 ("*5" is 0,
//    "5+" is 5).
// Deliberate differences, where MQ's behaviour would be undefined or crash: a formula past 2047 bytes is an error
// (MQ copies it into a 2048-byte buffer); text after a NUL byte is ignored, as MQ's C string ends there; numbers
// left side by side ("(1)(2)(3)") push past MQ's stack of n/2+2 slots, where here the last one is the result;
// -2147483648 \ -1 (a divide fault in MQ) gives -2147483648 and % gives 0; a shift count is taken modulo 32, as
// x86 does.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace hailstone::mq {

std::optional<double> calculate(std::string_view formula);

// MQ's GetIntFromString / GetInt64FromString / GetDoubleFromString: the number at the start of the trimmed text
// ("12abc" is 12), or `fail` when there is none or it does not fit.
int int_from_text(std::string_view text, int fail);
std::int64_t int64_from_text(std::string_view text, std::int64_t fail);
double double_from_text(std::string_view text, double fail);

// MQ's GetArg(dest, src, n, false, false, csv, separator): the n-th argument (n < 1 is the first) of `text`.
// Without a separator, arguments are split by spaces and tabs (and commas with `csv`); with one, by that character.
// A run of separators is one; a "quoted run" is part of one argument, its quotes dropped.
std::string get_arg(std::string_view text, int n, bool csv = false, char separator = 0);

} // namespace hailstone::mq
