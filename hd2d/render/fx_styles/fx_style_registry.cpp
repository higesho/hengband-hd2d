/*!
 * @file fx_style_registry.cpp
 * @brief `fx_style.h` の登録表の実体。7 つの系統（変愚の 4 つと Frox・幻想蛮怒・Sil-Q）の登録関数を呼んで、1 つの表にまとめる。
 */
#include "render/fx_styles/fx_style.h"

namespace hd2d {

namespace {

std::string lower_of(const std::string &text)
{
    std::string out = text;
    for (char &c : out) {
        if ((c >= 'A') && (c <= 'Z')) {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return out;
}

} // namespace

FxStyle &FxStyleRegistry::add(const char *attr, const FxColour &base, float smoke, float emissive)
{
    if (this->find(attr) != nullptr) {
        ++this->duplicates_;
    }
    FxStyle style;
    style.attr = attr;
    style.family = this->family_;
    style.base = base;
    style.smoke = smoke;
    style.emissive = emissive;
    this->styles_.push_back(style);
    return this->styles_.back();
}

FxStyle *FxStyleRegistry::find(const std::string &attr)
{
    for (FxStyle &style : this->styles_) {
        if (attr == style.attr) {
            return &style;
        }
    }
    return nullptr;
}

const FxStyle *FxStyleRegistry::find(const std::string &attr) const
{
    for (const FxStyle &style : this->styles_) {
        if (attr == style.attr) {
            return &style;
        }
    }
    return nullptr;
}

const FxStyleRegistry &fx_style_registry()
{
    static const FxStyleRegistry registry = [] {
        FxStyleRegistry r;
        r.set_family("liquid");
        fx_register_liquid(r);
        r.set_family("lightning");
        fx_register_lightning(r);
        r.set_family("elemental");
        fx_register_elemental(r);
        r.set_family("spirit");
        fx_register_spirit(r);
        r.set_family("frox");
        fx_register_frox(r);
        r.set_family("gensoband");
        fx_register_gensoband(r);
        r.set_family("silq");
        fx_register_silq(r);
        return r;
    }();
    return registry;
}

const FxStyle *fx_find_style(const std::string &attr)
{
    return fx_style_registry().find(lower_of(attr));
}

} // namespace hd2d
