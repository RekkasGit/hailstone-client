#include "game/tasks.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string_view>

#include "core/msvc_atoi.h"
#include "game/eq_strings.h"
#include "game/mechanics/tables.h"

namespace hailstone::game {

Task* TaskState::find(std::uint32_t type, std::uint32_t sequence) {
    for (Task& t : tasks)
        if (t.type == type && t.sequence == sequence) return &t;
    return nullptr;
}

const Task* TaskState::find(std::uint32_t type, std::uint32_t sequence) const {
    return const_cast<TaskState*>(this)->find(type, sequence);
}

const Task* TaskState::find_id(std::uint32_t task_id) const {
    for (const Task& t : tasks)
        if (t.task_id == task_id) return &t;
    return nullptr;
}

Task& TaskState::describe(Task t) {
    if (Task* old = find(t.type, t.sequence)) {
        // With open_window set the original empties the entry first (FUN_004808e0), its elements included.
        if (old->task_id == t.task_id && !t.open_window) t.elements = std::move(old->elements);
        *old = std::move(t);
        return *old;
    }
    tasks.push_back(std::move(t));
    return tasks.back();
}

bool TaskState::update_element(std::uint32_t type, std::uint32_t sequence, std::uint32_t task_id, TaskElement e) {
    Task* t = find(type, sequence);
    if (!t || t->task_id != task_id) return false;
    t->elements[e.activity_id] = std::move(e);
    return true;
}

bool TaskState::remove(std::uint32_t type, std::uint32_t sequence) {
    auto it = std::find_if(tasks.begin(), tasks.end(),
                           [&](const Task& t) { return t.type == type && t.sequence == sequence; });
    if (it == tasks.end()) return false;
    tasks.erase(it);
    return true;
}

namespace {

// The original's element type table (0xabf484, 13 entries): {type, eqstr with a target, eqstr without}.
struct ElementTemplate {
    std::int32_t type;
    std::uint32_t with_target, without_target;
};
constexpr ElementTemplate kElementTemplates[] = {
    {-1, 3474, 3474}, {2, 3468, 3468},  {3, 3498, 3470},   {1, 3469, 3469},   {4, 6001, 6001},
    {5, 6002, 6002},  {6, 6048, 6048},  {8, 6049, 6049},   {7, 6050, 6050},   {9, 6052, 6051},
    {10, 6054, 6053}, {11, 6117, 6117}, {13, 611, 610},
};

// strtok over the ";" delimiter (0xabed04 → 0x9c6e80), joining the names with " or " (0x9c6e98).
template <typename F> std::string join_ids(std::string_view list, F&& name_of) {
    std::string out;
    while (!list.empty()) {
        std::size_t end = list.find(';');
        std::string_view token = list.substr(0, end);
        list = end == std::string_view::npos ? std::string_view() : list.substr(end + 1);
        if (token.empty()) continue; // strtok skips empty tokens
        std::string name = name_of(core::msvc_atoi(token));
        if (name.empty()) continue;
        if (!out.empty()) out += " or ";
        out += name;
    }
    return out;
}

} // namespace

std::string element_description(const TaskElement& e, const EqStrings& strings,
                                const std::function<std::string(std::uint32_t)>& spell_name) {
    if (!e.description.empty()) return e.description;
    const ElementTemplate* t = nullptr;
    for (const ElementTemplate& x : kElementTemplates)
        if (x.type == e.type) t = &x;
    if (!t) return "Unknown index " + std::to_string(e.type) + ".";
    std::uint32_t id = e.target_name.empty() ? t->without_target : t->with_target;
    // The list strings are copied into 64-byte buffers (steve_strncpy 0x40) before strtok.
    std::string spells = join_ids(std::string_view(e.spell_list).substr(0, 63), [&](int s) {
        return s > 0 && spell_name ? spell_name(static_cast<std::uint32_t>(s)) : std::string();
    });
    std::string skills = join_ids(std::string_view(e.skill_list).substr(0, 63), [&](int s) {
        if (s == -1 || s >= 100 || s < 0) return std::string();
        return strings.format(mechanics::skill_name_eqstr(static_cast<std::uint32_t>(s)), {});
    });
    std::vector<std::string> args{std::to_string(e.goal_count), e.target_name, e.item_list, spells, skills};
    return strings.format(id, args);
}

std::string task_description_text(const Task& task, const std::function<std::string(const TaskElement&)>& element_text) {
    struct Section {
        std::vector<int> steps; // empty: always shown
        std::string text;
    };
    std::vector<Section> sections;
    std::string current;
    const std::string& d = task.description;
    std::size_t p = 0;
    while (p < d.size()) {
        if (d[p] != '[') {
            current += d[p++];
            continue;
        }
        if (!current.empty()) sections.push_back({{}, std::move(current)}), current.clear();
        ++p;
        Section s;
        // unk_NextCaretField(&p, ','): the field up to the next ',' (or the end), past it, read with atoi; 0
        // (not a number) records step 1000 and leaves p where the field began.
        for (;;) {
            std::size_t start = p;
            std::size_t comma = d.find(',', p);
            std::string field = d.substr(p, comma == std::string::npos ? std::string::npos : comma - p);
            p = comma == std::string::npos ? d.size() : comma + 1;
            int n = core::msvc_atoi(field);
            if (n == 0) {
                s.steps.push_back(1000);
                p = start;
                break;
            }
            s.steps.push_back(n);
            if (p >= d.size() || !std::isdigit(static_cast<unsigned char>(d[p]))) break;
        }
        std::size_t end = d.find(']', p);
        if (end == std::string::npos) end = d.size();
        s.text = d.substr(p, end - p);
        sections.push_back(std::move(s));
        p = end < d.size() ? end + 1 : end;
    }
    if (!current.empty()) sections.push_back({{}, std::move(current)});

    auto element = [&](int n) -> const TaskElement* {
        auto it = task.elements.find(static_cast<std::uint32_t>(n) - 1u);
        if (it == task.elements.end() || it->second.type == 0 || it->second.type == -1) return nullptr;
        return &it->second;
    };
    std::string out;
    for (const Section& s : sections) {
        bool exist = true, all_done = true, valid = true;
        for (int n : s.steps) {
            if (n < 1 || n > 20) {
                valid = false;
                break;
            }
            const TaskElement* e = element(n);
            exist = exist && e != nullptr;
            all_done = all_done && e && e->done_count >= e->goal_count;
        }
        if (!valid || !exist) continue;
        if (!s.steps.empty() && all_done) continue;
        out += s.text;
    }
    for (int n = 20; n >= 1; --n) {
        const TaskElement* e = element(n);
        if (!e) continue;
        std::string token = "%" + std::to_string(n);
        if (auto at = out.find(token); at != std::string::npos) out.replace(at, token.size(), element_text(*e));
    }
    if (out.size() > 4000) out.resize(4000);
    return out;
}

} // namespace hailstone::game

