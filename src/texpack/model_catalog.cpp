#include "texpack/model_catalog.h"

#include <algorithm>
#include <map>

#include "core/config.h"

namespace hailstone::texpack {

namespace fs = std::filesystem;

std::string_view model_kind_name(ModelKind kind) {
    switch (kind) {
    case ModelKind::Color: return "color";
    case ModelKind::Normal: return "normal";
    case ModelKind::Rough: return "rough";
    }
    return "color";
}

std::vector<ModelEntry> list_models(const std::vector<fs::path>& folders) {
    std::map<std::string, ModelEntry> byId;
    for (std::size_t f = 0; f < folders.size(); ++f) {
        std::error_code ec;
        for (fs::directory_iterator it(folders[f], ec), end; !ec && it != end; it.increment(ec)) {
            const fs::path& p = it->path();
            if (p.extension() != ".param") continue;
            fs::path bin = p;
            bin.replace_extension(".bin");
            if (!fs::is_regular_file(bin, ec)) continue;
            ModelEntry m;
            m.id = p.stem().string();
            m.title = m.id;
            m.param = p;
            m.bin = bin;
            m.user = f + 1 == folders.size() && folders.size() > 1;
            fs::path ini = p;
            ini.replace_extension(".ini");
            Config c;
            if (c.load(ini)) {
                m.title = c.get_string("title", m.id);
                std::string kind = c.get_string("kind", "color");
                m.kind = kind == "normal" ? ModelKind::Normal : kind == "rough" ? ModelKind::Rough : ModelKind::Color;
                m.scale = static_cast<std::uint32_t>(std::clamp(c.get_int("scale", 0), 0, 16));
                m.license = c.get_string("license");
                m.source = c.get_string("source");
                m.note = c.get_string("note");
            }
            byId[m.id] = std::move(m);
        }
    }
    std::vector<ModelEntry> out;
    for (auto& [id, m] : byId) out.push_back(std::move(m));
    std::sort(out.begin(), out.end(), [](const ModelEntry& a, const ModelEntry& b) {
        return a.kind != b.kind ? a.kind < b.kind : a.title < b.title;
    });
    return out;
}

const ModelEntry* find_model(const std::vector<ModelEntry>& models, std::string_view id) {
    for (const ModelEntry& m : models)
        if (m.id == id) return &m;
    return nullptr;
}

} // namespace hailstone::texpack
