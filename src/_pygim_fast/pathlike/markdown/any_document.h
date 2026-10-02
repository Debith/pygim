#pragma once
// pathlike/markdown/any_document.h — a document whose policies are chosen at run time, by name.
//
// Pybind-free. basic_document is a template on its dialect and slug rule;
// a caller that reads them from configuration (Python's
// `Document(text, dialect="commonmark", slugs="toc")`, a store's settings)
// names them instead. The names are positions in two policy packs, and the
// document is a std::variant over their product, built through a table of
// constructors at that position — there is no if-chain to grow when a
// dialect or a slug rule is added: it is one more type in its pack.
//
//     auto d = any_document::parse("# Hi\n", "commonmark", "toc", true);
//     d.dialect()                   -> "commonmark"
//     d.core().find(kind::heading)  -> {1}                  the policy-free half, no visit
//     d.visit([](const auto& x) { return x.html(); })    -> "<h1>Hi</h1>\n"
//     any_document::parse("", "rst", "toc", true)         -> std::invalid_argument:
//         "markdown dialect must be one of gfm, commonmark, got 'rst'"

#include <algorithm>
#include <array>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include "document.h"

namespace pygim::pathlike::markdown {

/// A pack of types, for folds and products.
template <class... Ts>
struct type_list {
    static constexpr std::size_t size = sizeof...(Ts);
};
template <class... A, class... B>
constexpr type_list<A..., B...> operator+(type_list<A...>, type_list<B...>) {
    return {};
}

/// The `name` of every type in a pack, in order.
template <class... Ts>
constexpr std::array<std::string_view, sizeof...(Ts)> names_of(type_list<Ts...>) {
    return {Ts::name...};
}

/// Where `name` is among `names`; std::invalid_argument naming the choices
/// when it is not: "markdown dialect must be one of gfm, commonmark, got 'rst'".
template <std::size_t N>
constexpr std::size_t position(const std::array<std::string_view, N>& names, std::string_view name, std::string_view what) {
    const auto it = std::find(names.begin(), names.end(), name);
    if (it != names.end()) return static_cast<std::size_t>(it - names.begin());
    std::string known;
    for (std::string_view n : names) (known += known.empty() ? "" : ", ") += n;
    throw std::invalid_argument(std::string(what) + " must be one of " + known + ", got '" + std::string(name) + "'");
}

using dialects = type_list<gfm, commonmark>;
using slug_rules = type_list<github_slug, toc_slug>;

template <class D, class... Ss>
constexpr type_list<basic_document<D, Ss>...> documents_of(type_list<Ss...>) {
    return {};
}
template <class... Ds, class Ss>
constexpr auto product(type_list<Ds...>, Ss rules) {
    return (documents_of<Ds>(rules) + ...);
}

class any_document {
public:
    /// Every (dialect, slug rule) document type, dialect-major: (gfm, github), (gfm, toc), ...
    using documents = decltype(product(dialects{}, slug_rules{}));

    /// Parses `source` under the named dialect and slug rule.
    [[nodiscard]] static any_document parse(std::string source, std::string_view dialect, std::string_view slugs,
                                            bool front_matter);

    /// The same policies over new text: what an edit becomes.
    [[nodiscard]] any_document reparse(std::string source) const {
        return std::visit([&](const auto& d) {
            using Doc = std::decay_t<decltype(d)>;
            return any_document(Doc(std::move(source), d.recognises_front_matter()));
        }, m_doc);
    }

    /// The policy-free half — lines, text, finding, edits — without a visit.
    [[nodiscard]] const document_core& core() const noexcept { return *m_core; }
    /// The policy-dependent half: `f` receives the basic_document itself.
    template <class F>
    decltype(auto) visit(F&& f) const {
        return std::visit(std::forward<F>(f), m_doc);
    }
    [[nodiscard]] std::string_view dialect() const {
        return visit([](const auto& d) { return std::decay_t<decltype(d)>::dialect::name; });
    }
    [[nodiscard]] std::string_view slugs() const {
        return visit([](const auto& d) { return std::decay_t<decltype(d)>::slug_policy::name; });
    }

    any_document(const any_document&) = delete;   // m_core points into m_doc
    any_document& operator=(const any_document&) = delete;
    any_document(any_document&& o) noexcept : m_doc(std::move(o.m_doc)), m_core(core_of(m_doc)) {}
    any_document& operator=(any_document&&) = delete;

private:
    template <class L>
    struct variant_over;
    template <class... Ts>
    struct variant_over<type_list<Ts...>> {
        using type = std::variant<Ts...>;
    };
    using variant = typename variant_over<documents>::type;

    variant m_doc;
    const document_core* m_core;

    template <class Doc>
    explicit any_document(Doc&& d) : m_doc(std::in_place_type<std::decay_t<Doc>>, std::forward<Doc>(d)), m_core(core_of(m_doc)) {}

    [[nodiscard]] static const document_core* core_of(const variant& v) noexcept {
        return std::visit([](const auto& d) -> const document_core* { return &d; }, v);
    }

    template <class Doc>
    static any_document construct(std::string&& source, bool front_matter) {
        return any_document(Doc(std::move(source), front_matter));
    }
    /// One constructor per document type, at its position in `documents`.
    template <class... Docs>
    static constexpr std::array<any_document (*)(std::string&&, bool), sizeof...(Docs)> constructors(type_list<Docs...>) {
        return {&construct<Docs>...};
    }
};

// Defined after the class: the constructor table is a constant of the complete class.
inline any_document any_document::parse(std::string source, std::string_view dialect, std::string_view slugs,
                                        bool front_matter) {
    static constexpr auto make = constructors(documents{});
    const std::size_t at = position(names_of(dialects{}), dialect, "markdown dialect") * slug_rules::size +
                           position(names_of(slug_rules{}), slugs, "markdown slugs");
    return make[at](std::move(source), front_matter);
}

}  // namespace pygim::pathlike::markdown
