#include <doctest/doctest.h>

#include "game/chat_windows.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>

using namespace hailstone::game;

TEST_CASE("Chat filters: every extended wire type selects its stable destination") {
    // Expected protocol facts are independent of the implementation's ranges.
    constexpr std::array<int, 53> destinations = {
        22, // 355: response-link palette entry, not a routing category
        57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71,
        72, 73, 74, 75, 76, 77, 78, 79, 80, 81,
        22, // 381: faction links
        84, 85,
        22, 22, 22, 22, // 384..387: item comparison/encounter palette entries
        88, 89,
        95, 95, 95, 95, 95, 95, 95, // seven consider colours
        40, 97, 98, 99, 100, 101, 102, 103, 104, 105, 106
    };
    for (std::size_t index = 0; index < destinations.size(); ++index) {
        const auto color = 355 + static_cast<std::int32_t>(index);
        CAPTURE(color);
        CHECK(chat_channel_of_color(color) == destinations[index]);
    }
    CHECK(chat_channel_of_color(408) == 107);
    CHECK(kChatChannelCount == 108);
    CHECK(kChatUserColorCount == 153);
}

TEST_CASE("Chat filters: reused colours, numbered channels and stock server DoTs") {
    constexpr std::array<std::array<int, 2>, 13> reassigned = {{
        {2, 90}, {4, 91}, {15, 92}, {13, 93},
        {274, 82}, {285, 83}, {286, 83}, {290, 94},
        {348, 86}, {349, 87}, {283, 96}, {325, 77}, {397, 40}
    }};
    for (const auto& [color, channel] : reassigned) {
        CAPTURE(color);
        CHECK(chat_channel_of_color(color) == channel);
    }
    for (int channel = 0; channel < 10; ++channel) {
        CHECK(chat_channel_of_color(291 + channel) == 12 + channel);
        CHECK(chat_channel_of_color(315 + channel) == 12 + channel);
    }

    constexpr std::array<std::array<int, 2>, 46> original = {{
        {256, 0}, {307, 0}, {257, 1}, {308, 1}, {258, 2}, {309, 2},
        {327, 3}, {259, 4}, {310, 4}, {345, 4}, {260, 5}, {311, 5},
        {261, 6}, {312, 6}, {262, 7}, {313, 7}, {263, 8}, {314, 8},
        {265, 9}, {264, 10}, {270, 11}, {267, 23}, {266, 24},
        {268, 25}, {279, 26}, {280, 27}, {277, 28}, {278, 29},
        {301, 30}, {271, 31}, {303, 32}, {304, 33}, {305, 34},
        {306, 35}, {288, 36}, {289, 37}, {302, 38}, {284, 39},
        {333, 41}, {287, 42}, {328, 43}, {331, 44}, {332, 45},
        {329, 46}, {334, 47}, {335, 49}
    }};
    for (const auto& [color, channel] : original) {
        CAPTURE(color);
        CHECK(chat_channel_of_color(color) == channel);
    }
    CHECK(chat_channel_of_color(281) == 50);
    CHECK(chat_channel_of_color(336) == 51);
    CHECK(chat_channel_of_color(337) == 52);
    CHECK(chat_channel_of_color(338) == 53);
    CHECK(chat_channel_of_color(342) == 54);
    CHECK(chat_channel_of_color(346) == 55);
    CHECK(chat_channel_of_color(350) == 56);
    for (const auto color : {-1, 0, 1, 3, 269, 341, 347, 351, 354, 409,
                            std::numeric_limits<int>::max(), std::numeric_limits<int>::min()}) {
        CAPTURE(color);
        CHECK(chat_channel_of_color(color) == kChatChannelOther);
    }
}

TEST_CASE("Chat filters: menu descriptors cover every destination and form an acyclic hierarchy") {
    std::array<int, kChatChannelCount> destinations{};
    for (const auto& item : chat_filter_items()) {
        REQUIRE(item.channel < kChatChannelCount);
        ++destinations[item.channel];
        if (item.channel >= 57) CHECK_FALSE(item.label.empty());
    }
    for (int channel = 0; channel < kChatChannelCount; ++channel) {
        CAPTURE(channel);
        CHECK(destinations[static_cast<std::size_t>(channel)] == (channel == 48 ? 0 : 1));
    }
    const auto groups = chat_filter_groups();
    CHECK(groups.size() == 10);
    for (const auto& group : groups) {
        CHECK_FALSE(group.label.empty());
        CHECK(group.menu != ChatFilterMenu::Filters);
        CHECK(std::count_if(groups.begin(), groups.end(), [&](const auto& other) {
            return other.menu == group.menu;
        }) == 1);
        auto parent = group.parent;
        int depth = 0;
        while (parent != ChatFilterMenu::Filters && depth <= 10) {
            auto found = std::find_if(groups.begin(), groups.end(), [&](const auto& other) {
                return other.menu == parent;
            });
            REQUIRE(found != groups.end());
            parent = found->parent;
            ++depth;
        }
        CHECK(depth <= 10);
    }
    for (const auto& item : chat_filter_items()) {
        if (item.menu == ChatFilterMenu::Filters) continue;
        CHECK(std::any_of(groups.begin(), groups.end(), [&](const auto& group) {
            return group.menu == item.menu;
        }));
    }
    CHECK(chat_filter_all_eqstr(ChatFilterMenu::Melee) == 6041);
    CHECK(chat_filter_all_eqstr(ChatFilterMenu::Healing) == 0);
    CHECK(chat_filter_all_eqstr(ChatFilterMenu::DamageOverTime) == 0);
}
