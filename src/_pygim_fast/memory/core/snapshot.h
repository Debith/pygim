// memory/core/snapshot.h — the index a read answers from (04 §1).
//
// A snapshot is built by replaying audit rows (03, Decision 1). It is copied
// by value to build the next one: every part is held behind a
// shared_ptr<const ...>, so the copy shares everything and `apply` replaces
// only the parts a row touches (04 §2). A published snapshot is never mutated.
// Pybind-free; no I/O.
#pragma once

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "../../mapping/id_set.h"
#include "ids.h"
#include "knowledge.h"
#include "provenance.h"
#include "retrieval.h"
#include "taxonomy.h"

namespace pygim::memory {

using memory_set = mapping::basic_id_set<std::uint32_t>;
using tag_set = mapping::basic_id_set<std::uint16_t>;

class snapshot {
public:
    using instance = std::pair<std::uint16_t, row_id>;  // (tag, the row that linked it)

    explicit snapshot(std::shared_ptr<const taxonomy> tax)
        : m_tax(std::move(tax)),
          m_retired(std::make_shared<const memory_set>()),
          m_keys(std::make_shared<const key_map>()) {
        const auto empty = std::make_shared<const memory_set>();
        m_postings.assign(m_tax->tags(), empty);
    }

    // ── identity ──────────────────────────────────────────────────────────

    [[nodiscard]] const row_id& head() const noexcept { return m_head; }
    [[nodiscard]] std::uint64_t version() const noexcept { return m_version; }
    [[nodiscard]] const taxonomy& tax() const noexcept { return *m_tax; }
    [[nodiscard]] const std::shared_ptr<const taxonomy>& tax_ptr() const noexcept { return m_tax; }

    // ── contents ──────────────────────────────────────────────────────────

    [[nodiscard]] std::size_t size() const noexcept { return m_memories.size(); }
    [[nodiscard]] std::size_t heads() const noexcept { return m_memories.size() - m_retired->size(); }
    [[nodiscard]] const memory_record& record(memory_id m) const { return *m_memories[m.value()]; }
    [[nodiscard]] bool is_head(memory_id m) const { return !m_retired->has(m.value()); }
    [[nodiscard]] const tag_set& tags_of(memory_id m) const { return *m_forward[m.value()]; }
    [[nodiscard]] const memory_set& carrying(tag_id t) const { return *m_postings[t.value()]; }
    [[nodiscard]] const std::vector<memory_id>& superseded_by(memory_id m) const { return *m_superseded_by[m.value()]; }
    [[nodiscard]] const std::vector<memory_id>& generalised_by(memory_id m) const { return *m_generalised_by[m.value()]; }

    [[nodiscard]] std::optional<memory_id> find(const row_id& key) const {
        const auto it = m_keys->find(key);
        if (it == m_keys->end()) return std::nullopt;
        return memory_id(it->second);
    }

    /// The live association instances of `t` on `m` — what an unlink removes.
    [[nodiscard]] std::vector<row_id> instances(memory_id m, tag_id t) const {
        std::vector<row_id> out;
        for (const auto& [tag, by] : *m_instances[m.value()])
            if (tag == t.value()) out.push_back(by);
        return out;
    }

    /// The current heads that carry exactly this content.
    [[nodiscard]] std::vector<memory_id> heads_with_content(const digest& content) const {
        std::vector<memory_id> out;
        for (std::size_t i = 0; i < m_memories.size(); ++i)
            if (!m_retired->has(static_cast<std::uint32_t>(i)) && m_memories[i]->content == content)
                out.push_back(memory_id(static_cast<std::uint32_t>(i)));
        return out;
    }

    /// Every slug a chain holds, for choosing a new one.
    [[nodiscard]] bool slug_taken(std::string_view slug) const {
        for (const auto& m : m_memories)
            if (m->slug == slug) return true;
        return false;
    }

    std::vector<pending_proposal> proposals;
    std::vector<review_item> reviews;

    // ── replay ────────────────────────────────────────────────────────────

    /// Applies one row to this snapshot. The caller copies the previous
    /// snapshot first; only the parts this row touches are replaced.
    void apply(const row& r) {
        if (r.op == ops::write) apply_write(r);
        else if (r.op == ops::link) apply_link(r);
        else if (r.op == ops::unlink) apply_unlink(r);
        else if (r.op == ops::retire) apply_retire(r);
        // taxonomy and merge rows are history: they move the head, nothing else
        m_head = r.id;
        ++m_version;
    }

    // ── retrieval (04 §3) ─────────────────────────────────────────────────

    /// Steps 2–4: the union of each hard dimension's values (and its `any`),
    /// the intersection across dimensions, then only heads.
    [[nodiscard]] memory_set candidates(const std::vector<tag_id>& hard) const {
        std::vector<std::pair<std::uint8_t, std::vector<tag_id>>> groups;
        for (const auto t : hard) {
            const auto d = m_tax->dimension_of(t).value();
            auto it = std::find_if(groups.begin(), groups.end(), [d](const auto& g) { return g.first == d; });
            if (it == groups.end()) groups.push_back({d, {t}});
            else it->second.push_back(t);
        }
        std::optional<memory_set> acc;
        for (const auto& [d, ts] : groups) {
            memory_set u;
            for (const auto t : ts) u = u.united(*m_postings[t.value()]);
            if (const auto a = m_tax->any_of(dimension_id(d))) u = u.united(*m_postings[a->value()]);
            acc = acc ? acc->intersected(u) : u;
        }
        if (!acc) return {};
        return acc->subtracted(*m_retired);
    }

    /// Steps 5–10 of a read: score, rank, the procedure slot, the budget.
    [[nodiscard]] context read(const resolved_query& q) const {
        context ctx;
        ctx.corpus = static_cast<std::uint32_t>(heads());
        const memory_set cand = candidates(q.hard);
        std::vector<std::uint32_t> ids(cand.members().begin(), cand.members().end());
        std::sort(ids.begin(), ids.end());
        ctx.candidates = static_cast<std::uint32_t>(ids.size());

        const std::optional<std::uint32_t> slot = procedure_slot(q, cand, ctx.procedure_note);
        if (slot) ctx.procedure = memory_id(*slot);

        // The fold (overview §4.11, 04 §3.7): a candidate one of whose generalisations is also a
        // candidate gives up its own place and is listed under that generalisation as evidence.
        // It happens before ranking and the budget, so it depends on neither; the procedure slot is
        // never folded, and a retired generalisation is no candidate, so its instances unfold.
        std::unordered_map<std::uint32_t, std::vector<memory_id>> under;
        std::vector<std::uint32_t> placed;
        placed.reserve(ids.size());
        for (const auto id : ids) {
            if (slot && id == *slot) continue;
            bool folded = false;
            for (const auto g : *m_generalised_by[id])
                if (cand.has(g.value())) {
                    under[g.value()].push_back(memory_id(id));
                    folded = true;
                }
            if (folded) ++ctx.folded;
            else placed.push_back(id);
        }
        std::vector<match> ranked;
        ranked.reserve(placed.size());
        for (const auto id : placed) {
            match m = score(memory_id(id), q);
            if (const auto it = under.find(id); it != under.end()) m.evidence = it->second;
            ranked.push_back(std::move(m));
        }
        if (slot)
            if (const auto it = under.find(*slot); it != under.end()) ctx.procedure_evidence = it->second;
        std::sort(ranked.begin(), ranked.end(), [](const match& a, const match& b) {
            if (a.final_score != b.final_score) return a.final_score > b.final_score;
            if (a.soft_hits != b.soft_hits) return a.soft_hits > b.soft_hits;
            return a.id < b.id;
        });
        for (std::size_t i = 0; i < ranked.size(); ++i) ranked[i].rank = static_cast<std::uint32_t>(i + 1);

        std::uint32_t used = 0;
        if (slot) {
            used = m_memories[*slot]->tokens;
            if (q.budget && used > q.budget) {
                ctx.over_budget = true;
                ctx.tokens = used;
                ctx.skipped = std::move(ranked);
                return ctx;
            }
        }
        for (auto& m : ranked) {
            if (ctx.selected.size() >= q.max_memories || (q.budget && used + m.tokens > q.budget)) {
                ctx.skipped.push_back(m);
                continue;
            }
            used += m.tokens;
            ctx.selected.push_back(m);
        }
        ctx.tokens = used;
        return ctx;
    }

private:
    using key_map = std::unordered_map<row_id, std::uint32_t, digest_hash>;

    [[nodiscard]] match score(memory_id m, const resolved_query& q) const {
        match out;
        out.id = m;
        out.tokens = m_memories[m.value()]->tokens;
        const tag_set& tags = *m_forward[m.value()];
        std::vector<std::uint8_t> matched_dims;
        for (const auto t : q.hard) {
            if (tags.has(t.value())) {
                out.hard_matched.push_back(t);
                matched_dims.push_back(m_tax->dimension_of(t).value());
            }
        }
        for (const auto t : q.hard) {
            const auto d = m_tax->dimension_of(t);
            if (std::find(matched_dims.begin(), matched_dims.end(), d.value()) != matched_dims.end()) continue;
            if (const auto a = m_tax->any_of(d); a && tags.has(a->value())) {
                out.hard_matched.push_back(*a);
                matched_dims.push_back(d.value());
            }
        }
        for (const auto t : q.soft) {
            if (!m_tax->info(t).any && tags.has(t.value())) {
                out.soft_matched.push_back(t);
                out.soft_score += m_tax->weight(m_tax->dimension_of(t));
                ++out.soft_hits;
            } else {
                out.soft_missed.push_back(t);
            }
        }
        out.final_score = out.soft_score * 1'000'000 + out.semantic_score;
        return out;
    }

    /// Step 8 (04 §3.6): the head procedure for the query's one artifact and
    /// one task, among the candidates; `note` says why when there is none.
    [[nodiscard]] std::optional<std::uint32_t> procedure_slot(const resolved_query& q, const memory_set& cand,
                                                              std::string& note) const {
        const auto artifact = m_tax->dimension("artifact");
        const auto task = m_tax->dimension("task");
        const auto procedure = m_tax->tag("kind=procedure");
        if (!artifact || !task || !procedure) return std::nullopt;
        std::vector<tag_id> arts, tasks;
        for (const auto t : q.hard) {
            if (m_tax->info(t).any) continue;
            if (m_tax->dimension_of(t) == *artifact) arts.push_back(t);
            if (m_tax->dimension_of(t) == *task) tasks.push_back(t);
        }
        if (arts.size() != 1 || tasks.size() != 1) {
            note = arts.size() != 1 ? "no procedure slot: " + decimal(arts.size()) + " artifact values are hard"
                                    : "no procedure slot: " + decimal(tasks.size()) + " task values are hard";
            return std::nullopt;
        }
        const memory_set p = m_postings[procedure->value()]
                                 ->intersected(*m_postings[arts[0].value()])
                                 .intersected(*m_postings[tasks[0].value()])
                                 .intersected(cand);
        const std::string pair = m_tax->info(arts[0]).qualified + " and " + m_tax->info(tasks[0]).qualified;
        if (p.empty()) {
            note = "no procedure yet for " + pair;
            return std::nullopt;
        }
        std::vector<std::uint32_t> ids(p.members().begin(), p.members().end());
        std::sort(ids.begin(), ids.end());
        if (ids.size() > 1)
            note = pair + " have " + decimal(ids.size()) + " procedures: #" + decimal(ids[0]) +
                   " is placed, the others rank — merge them";
        return ids[0];
    }

    void add_posting(std::uint16_t t, std::uint32_t m) {
        auto p = std::make_shared<memory_set>(*m_postings[t]);
        p->note(m);
        m_postings[t] = std::move(p);
    }
    void remove_posting(std::uint16_t t, std::uint32_t m) {
        m_postings[t] = std::make_shared<const memory_set>(m_postings[t]->where([m](std::uint32_t id) { return id != m; }));
    }

    std::optional<std::uint32_t> resolve(std::string_view key_hex, const row& r) {
        const auto key = digest::from_hex(key_hex);
        const auto m = key ? find(*key) : std::nullopt;
        if (!m) {
            reviews.push_back({"dangling", "row " + r.id.hex().substr(0, 12) + " (" + r.op + ") names memory " +
                                               std::string(key_hex.substr(0, 12)) + ", which no row created"});
            return std::nullopt;
        }
        return m->value();
    }
    std::optional<std::uint16_t> resolve_tag(std::string_view name, const row& r) {
        const auto t = m_tax->tag(name);
        if (!t) {
            reviews.push_back({"unknown tag", "row " + r.id.hex().substr(0, 12) + " (" + r.op + ") names " +
                                                  std::string(name) + ", which the vocabulary no longer has"});
            return std::nullopt;
        }
        return t->value();
    }

    void apply_write(const row& r) {
        const auto m = static_cast<std::uint32_t>(m_memories.size());
        auto rec = std::make_shared<memory_record>();
        rec->key = r.id;
        rec->content = digest::from_hex(r.get("content")).value_or(digest{});
        rec->slug = std::string(r.get("slug"));
        rec->title = std::string(r.get("title"));
        rec->origin = std::string(r.get("origin"));
        rec->created = r.time;
        rec->author = std::string(r.get("author"));
        rec->reason = std::string(r.get("reason"));
        rec->corpus = std::string(r.get("corpus"));
        rec->revision = std::string(r.get("revision"));
        std::uint64_t bytes = 0;
        for (const char c : r.get("length")) bytes = bytes * 10 + static_cast<std::uint64_t>(c - '0');
        rec->tokens = token_estimate(bytes);
        for (const char c : r.get("session")) rec->session = rec->session * 10 + static_cast<std::uint64_t>(c - '0');
        for (const char c : r.get("turn")) rec->turn = rec->turn * 10 + static_cast<std::uint32_t>(c - '0');
        for (const auto s : r.all("seen"))
            if (auto d = digest::from_hex(s)) rec->seen.push_back(*d);
        for (const auto c : r.all("cite")) rec->cites.emplace_back(c);

        auto fwd = std::make_shared<tag_set>();
        auto inst = std::make_shared<std::vector<instance>>();
        for (const auto name : r.all("tag")) {
            const auto t = resolve_tag(name, r);
            if (!t) continue;
            if (fwd->note(*t)) add_posting(*t, m);
            inst->emplace_back(*t, r.id);
        }
        m_memories.push_back(rec);
        m_forward.push_back(std::move(fwd));
        m_instances.push_back(std::move(inst));
        m_superseded_by.push_back(std::make_shared<const std::vector<memory_id>>());
        m_generalised_by.push_back(std::make_shared<const std::vector<memory_id>>());
        auto keys = std::make_shared<key_map>(*m_keys);
        keys->emplace(r.id, m);
        m_keys = std::move(keys);

        for (const auto k : r.all("supersedes")) {
            const auto old = resolve(k, r);
            if (!old) continue;
            rec->supersedes.push_back(m_memories[*old]->key);
            auto by = std::make_shared<std::vector<memory_id>>(*m_superseded_by[*old]);
            by->push_back(memory_id(m));
            if (by->size() > 1)
                reviews.push_back({"forked chain", "#" + decimal(*old) + " (" + m_memories[*old]->slug +
                                                       ") is superseded by " + decimal(by->size()) +
                                                       " memories — two corrections of one note; merge them"});
            m_superseded_by[*old] = std::move(by);
            retire_id(*old);
        }
        // Evidence is kept (01 §8): a generalisation points at its instances and retires none of them.
        for (const auto k : r.all("generalises")) {
            const auto instance = resolve(k, r);
            if (!instance) continue;
            rec->generalises.push_back(m_memories[*instance]->key);
            auto by = std::make_shared<std::vector<memory_id>>(*m_generalised_by[*instance]);
            by->push_back(memory_id(m));
            m_generalised_by[*instance] = std::move(by);
        }
        fold_proposals(r);
    }

    void apply_link(const row& r) {
        const auto m = resolve(r.get("memory"), r);
        const auto t = resolve_tag(r.get("tag"), r);
        if (!m || !t) return;
        auto inst = std::make_shared<std::vector<instance>>(*m_instances[*m]);
        inst->emplace_back(*t, r.id);
        m_instances[*m] = std::move(inst);
        if (!m_forward[*m]->has(*t)) {
            auto fwd = std::make_shared<tag_set>(*m_forward[*m]);
            fwd->note(*t);
            m_forward[*m] = std::move(fwd);
            add_posting(*t, *m);
        }
    }

    /// Add wins (03 §5): an unlink removes only the instances it names — the
    /// ones its writer had seen — so a link made concurrently survives.
    void apply_unlink(const row& r) {
        const auto m = resolve(r.get("memory"), r);
        const auto t = resolve_tag(r.get("tag"), r);
        if (!m || !t) return;
        const auto removes = r.all("removes");
        auto inst = std::make_shared<std::vector<instance>>();
        bool still = false;
        for (const auto& [tag, by] : *m_instances[*m]) {
            const bool gone = tag == *t && std::find(removes.begin(), removes.end(), by.hex()) != removes.end();
            if (!gone) inst->emplace_back(tag, by);
            if (!gone && tag == *t) still = true;
        }
        m_instances[*m] = std::move(inst);
        if (!still && m_forward[*m]->has(*t)) {
            const auto tt = *t;
            m_forward[*m] = std::make_shared<const tag_set>(m_forward[*m]->where([tt](std::uint16_t x) { return x != tt; }));
            remove_posting(*t, *m);
        }
    }

    void apply_retire(const row& r) {
        if (const auto m = resolve(r.get("memory"), r)) retire_id(*m);
    }

    void retire_id(std::uint32_t m) {
        if (m_retired->has(m)) return;
        auto s = std::make_shared<memory_set>(*m_retired);
        s->note(m);
        m_retired = std::move(s);
    }

    void fold_proposals(const row& r) {
        for (std::size_t n = 0;; ++n) {
            const std::string p = "proposal." + decimal(n) + ".";
            const auto concept_name = r.get(p + "concept");
            if (concept_name.empty()) break;
            const auto dim = r.get(p + "dimension");
            const auto k = pending_proposal::key(concept_name, dim);
            auto it = std::find_if(proposals.begin(), proposals.end(),
                                   [&](const pending_proposal& x) { return pending_proposal::key(x.concept_name, x.dimension) == k; });
            if (it == proposals.end()) {
                pending_proposal pp;
                pp.concept_name = std::string(concept_name);
                pp.dimension = std::string(dim);
                pp.entry = {std::string(r.get(p + "brief")), std::string(r.get(p + "full")), std::string(r.get(p + "when")),
                            std::string(r.get(p + "when_not")), std::string(r.get(p + "example"))};
                proposals.push_back(std::move(pp));
                it = proposals.end() - 1;
            }
            it->asked_by.push_back(r.id);
        }
    }

    std::shared_ptr<const taxonomy> m_tax;
    row_id m_head;
    std::uint64_t m_version = 0;
    std::vector<std::shared_ptr<const memory_record>> m_memories;
    std::vector<std::shared_ptr<const tag_set>> m_forward;
    std::vector<std::shared_ptr<const std::vector<instance>>> m_instances;
    std::vector<std::shared_ptr<const std::vector<memory_id>>> m_superseded_by;
    std::vector<std::shared_ptr<const std::vector<memory_id>>> m_generalised_by;
    std::vector<std::shared_ptr<const memory_set>> m_postings;
    std::shared_ptr<const memory_set> m_retired;
    std::shared_ptr<const key_map> m_keys;
};

}  // namespace pygim::memory
