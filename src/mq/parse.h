#pragma once

// MacroQuest's ${...} text (ParseMacroData): every ${TLO[index].Member[index]...} in a line is replaced by its value's
// text, innermost first, so an index may hold another ${...}: "${Me.Buff[${n}].Duration.TotalSeconds}". Unknown
// names give NULL, as in MQ. A value's text is never parsed again: a spawn named "${Me.Name}" stays that text (MQ2
// re-scans the line after each replacement; a deliberate difference, so what other players and the server name
// things cannot read the game state with the player's or a script's permissions).

#include <string>
#include <string_view>

#include "mq/tlo.h"

namespace hailstone::mq {

// One expression without the ${ }: "Me.PctHPs", "Spawn[npc radius 50].ID".
Value evaluate(const Context& ctx, std::string_view expr);
// The line with each ${...} replaced.
std::string parse_macro_data(const Context& ctx, std::string_view line);

} // namespace hailstone::mq
