// memory/core/service.h — the memory service over any store (overview §7).
//
// The service is dumb on purpose (overview §4.7): it resolves names, runs the
// four checks of a write, commits rows and keeps the snapshot current. Every
// check runs against the head a row will follow, under the store's commit
// lock, so the guarantees hold for every process sharing a store (03 §4.2).
// Pybind-free; all I/O goes through the store.
#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "ids.h"
#include "knowledge.h"
#include "provenance.h"
#include "retrieval.h"
#include "snapshot.h"
#include "taxonomy.h"

namespace pygim::memory {

/// A loaded vocabulary, or everything wrong with it (each message names its
/// file and line — the definition of done's rule).
struct taxonomy_load {
    std::shared_ptr<const taxonomy> tax;
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
};

/// A vocabulary that does not load; the message lists every failure.
struct load_error : std::runtime_error {
    using std::runtime_error::runtime_error;
};

/// What every store strategy provides (03 §7).
template <class S>
concept memory_store = requires(S& s, const row& r, const usage_record& u, const receipt& rc, const digest& d,
                                std::string_view sv, const memory_record& mr, const std::vector<std::string>& names) {
    { s.load_taxonomy() } -> std::same_as<taxonomy_load>;
    { s.load_frozen_taxonomy(d) } -> std::same_as<std::optional<taxonomy_load>>;
    { s.freeze_taxonomy(d) } -> std::same_as<void>;
    { s.rows() } -> std::same_as<std::vector<row>>;      // every row, from every clone
    { s.new_rows() } -> std::same_as<std::vector<row>>;  // rows others appended since the last call
    { s.append(r) } -> std::same_as<void>;               // the commit point
    { s.put_object(d, sv) } -> std::same_as<void>;
    { s.object(d) } -> std::same_as<std::optional<std::string>>;
    { s.lock() };                                        // the commit lock, held by the returned guard
    { s.clone() } -> std::convertible_to<std::string>;
    { s.next_seq() } -> std::same_as<std::uint64_t>;
    { s.next_session() } -> std::same_as<std::uint64_t>;
    { s.now() } -> std::same_as<std::string>;
    { s.append_usage(u) } -> std::same_as<void>;
    { s.usage() } -> std::same_as<std::vector<usage_record>>;
    { s.append_receipt(rc) } -> std::same_as<void>;
    { s.write_view(mr, sv, names) } -> std::same_as<void>;
    { s.remove_view(sv) } -> std::same_as<void>;
};

/// A check that failed, in facts rather than opinions (overview §4.5).
struct refusal {
    std::string kind;
    std::string message;
    std::vector<std::string> facts;
};

struct write_outcome {
    bool ok = false;
    refusal why;
    memory_id id;
    row_id key;
    std::string slug;
    std::uint64_t version = 0;
    std::string verdict;  // a merge's recollection-failure kind (04, overview §4.7)
};

struct op_outcome {
    bool ok = false;
    refusal why;
    std::string message;
    std::uint64_t version = 0;
    bool promoted = false;
};

struct read_outcome {
    bool ok = false;
    refusal why;
    std::shared_ptr<const snapshot> snap;
    context ctx;
    receipt rec;
};

struct corpus_block {
    std::string slug;
    std::string title;
    std::vector<std::string> tags;
    std::string text;
    std::size_t line = 0;
};

struct ingest_outcome {
    std::size_t added = 0;
    std::size_t superseded = 0;
    std::size_t unchanged = 0;
    std::vector<std::string> refused;  // "notes/spells.md:118: level_band=tier2 is not a value"
};

struct service_options {
    std::uint32_t promotion_threshold = 3;  // useful reports before a tag is learned (07, open)
};

template <memory_store Store>
class memory_service {
public:
    explicit memory_service(Store& store, service_options opts = {}) : m_store(store), m_opts(opts) {}

    // ── opening (00a, Scenario 0) ─────────────────────────────────────────

    /// Loads the vocabulary and every row, joins divergent histories with a
    /// merge row, records a vocabulary that changed since the last row said
    /// so, links memories whose proposals have been accepted, and publishes.
    void open() {
        taxonomy_load t = m_store.load_taxonomy();
        if (!t.errors.empty()) throw load_error(join(t.errors, "\n"));
        m_tax = t.tax;
        m_load_reviews.clear();
        for (auto& w : t.warnings) m_load_reviews.push_back({"vocabulary", std::move(w)});

        auto guard = m_store.lock();
        std::vector<row> rows = m_store.rows();
        m_applied.clear();
        for (const auto& r : rows) m_applied.insert(r.id);
        publish(std::make_shared<const snapshot>(replay(rows, std::nullopt)));

        const auto heads = head_rows(rows);
        if (heads.size() > 1) {
            row r;
            r.op = std::string(ops::merge);
            r.parents = heads;
            append_locked(std::move(r));
        }
        const digest version = m_tax->version();
        if (last_taxonomy_version(rows) != version) {
            m_store.freeze_taxonomy(version);
            row r;
            r.op = std::string(ops::taxonomy);
            r.add("version", version.hex());
            r.add("reason", rows.empty() ? "the repository's first vocabulary" : "the vocabulary files changed since the last recorded version");
            append_locked(std::move(r));
        }
        link_accepted_proposals();
    }

    /// The snapshot readers answer from; never waits on a writer for longer
    /// than a pointer copy.
    [[nodiscard]] std::shared_ptr<const snapshot> current() const {
        std::lock_guard<std::mutex> g(m_publish);
        return m_current;
    }

    [[nodiscard]] const taxonomy& tax() const { return *m_tax; }
    [[nodiscard]] std::vector<review_item> reviews() const {
        auto out = m_load_reviews;
        const auto s = current();
        out.insert(out.end(), s->reviews.begin(), s->reviews.end());
        return out;
    }
    [[nodiscard]] std::uint64_t new_session() { return m_store.next_session(); }

    /// Folds in rows other processes committed since this one last looked.
    void refresh() {
        auto guard = m_store.lock();
        catch_up();
    }

    // ── reading (04 §3) ───────────────────────────────────────────────────

    read_outcome read(const query& q, std::uint64_t session = 0) {
        read_outcome out;
        out.snap = current();
        const snapshot& s = *out.snap;
        resolved_query rq;
        rq.max_memories = q.max_memories;
        rq.budget = q.budget;
        if (!resolve_query_tags(s.tax(), q.hard, rq.hard, out.why) || !resolve_query_tags(s.tax(), q.soft, rq.soft, out.why))
            return out;
        if (rq.hard.empty()) {
            out.why = {"no hard tags", "a read needs at least one hard tag — without one every memory is a candidate", {}};
            return out;
        }
        out.ctx = s.read(rq);
        out.ok = true;
        out.rec.snapshot = s.head();
        out.rec.version = s.version();
        out.rec.taxonomy = s.tax().version();
        out.rec.asked = q;
        out.rec.time = m_store.now();
        out.rec.session = session;
        if (out.ctx.procedure) out.rec.keys.push_back(s.record(*out.ctx.procedure).key);
        for (const auto& m : out.ctx.selected) out.rec.keys.push_back(s.record(m.id).key);
        m_store.append_receipt(out.rec);
        for (const auto& m : out.ctx.selected) m_store.append_usage({s.record(m.id).key, {}, "included", out.rec.time, session, {}});
        if (out.ctx.procedure) m_store.append_usage({s.record(*out.ctx.procedure).key, {}, "included", out.rec.time, session, {}});
        return out;
    }

    /// Reruns a receipt against the snapshot and vocabulary it pinned (04 §5).
    read_outcome rerun(const receipt& rec) {
        read_outcome out;
        std::shared_ptr<const taxonomy> tax = m_tax;
        if (rec.taxonomy != m_tax->version()) {
            auto frozen = m_store.load_frozen_taxonomy(rec.taxonomy);
            if (!frozen || !frozen->errors.empty()) {
                out.why = {"vocabulary missing", "the vocabulary " + rec.taxonomy.hex().substr(0, 12) + " was never frozen in this store", {}};
                return out;
            }
            tax = frozen->tax;
        }
        std::vector<row> rows = m_store.rows();
        const bool known = std::any_of(rows.begin(), rows.end(), [&](const row& r) { return r.id == rec.snapshot; });
        if (!known) {
            out.why = {"snapshot missing", "no row " + rec.snapshot.hex().substr(0, 12) + " in this store", {}};
            return out;
        }
        auto snap = std::make_shared<const snapshot>(replay(rows, rec.snapshot, tax));
        out.snap = snap;
        resolved_query rq;
        rq.max_memories = rec.asked.max_memories;
        rq.budget = rec.asked.budget;
        if (!resolve_query_tags(*tax, rec.asked.hard, rq.hard, out.why) || !resolve_query_tags(*tax, rec.asked.soft, rq.soft, out.why))
            return out;
        out.ctx = snap->read(rq);
        out.ok = true;
        out.rec = rec;
        out.rec.keys.clear();
        if (out.ctx.procedure) out.rec.keys.push_back(snap->record(*out.ctx.procedure).key);
        for (const auto& m : out.ctx.selected) out.rec.keys.push_back(snap->record(m.id).key);
        return out;
    }

    // ── writing (overview §4.5) ───────────────────────────────────────────

    write_outcome remember(write_request w) {
        write_outcome out;
        if (w.title.empty() || w.text.empty()) {
            out.why = {"empty", "a memory needs a title and text", {}};
            return out;
        }
        auto guard = m_store.lock();
        catch_up();
        const auto snap = current();
        const snapshot& s = *snap;

        // 1. closed vocabulary — and every hard question answered, or the memory could never be found
        std::vector<tag_id> tags;
        if (!resolve_write_tags(s, w, tags, out.why)) return out;
        for (const auto& p : w.proposals) {
            if (p.concept_name.empty()) {
                out.why = {"proposal", "a proposal needs a concept", {}};
                return out;
            }
            if (auto why = incomplete(p.entry, p.concept_name); !why.empty()) {
                out.why = {"proposal", "proposal \"" + p.concept_name + "\": " + why, {}};
                return out;
            }
        }
        std::vector<memory_id> supersedes, seen, generalises;
        if (!resolve_refs(s, w.supersedes, supersedes, out.why) || !resolve_refs(s, w.seen, seen, out.why) ||
            !resolve_refs(s, w.generalises, generalises, out.why))
            return out;

        // 3. head only
        for (const auto m : supersedes) {
            if (!s.is_head(m)) {
                out.why = {"not a head", "#" + decimal(m.value()) + " has been superseded — supersede the current head", {}};
                for (const auto h : chain_heads(s, m)) out.why.facts.push_back(describe(s, h));
                return out;
            }
        }
        // a generalisation (overview §4.11): several heads, kept as evidence, and covered
        if (!generalises.empty() && !check_generalisation(s, tags, supersedes, generalises, out.why)) return out;
        for (const auto m : generalises)  // naming an instance is having read it
            if (std::find(seen.begin(), seen.end(), m) == seen.end()) seen.push_back(m);
        // 2. no unread write — a write that starts a chain must have read the space it lands in
        if (supersedes.empty() && w.origin != "seed") {
            const memory_set space = s.candidates(write_space(s, tags));
            std::vector<std::uint32_t> unseen;
            for (const auto id : space.members())
                if (std::find(seen.begin(), seen.end(), memory_id(id)) == seen.end()) unseen.push_back(id);
            if (!unseen.empty()) {
                std::sort(unseen.begin(), unseen.end());
                out.why = {"unread", decimal(unseen.size()) + (unseen.size() == 1 ? " memory in this space was" : " memories in this space were") +
                                         " not read first — read them, then decide: extend one, learn instead, or start a chain", {}};
                for (const auto id : unseen) out.why.facts.push_back(describe(s, memory_id(id)));
                return out;
            }
        }
        // 4. identical content
        const digest content = digest::of(w.text);
        if (const auto same = s.heads_with_content(content); !same.empty()) {
            out.why = {"identical", "this exact text is already a head — report it useful instead", {describe(s, same[0])}};
            return out;
        }

        row r;
        r.op = std::string(ops::write);
        r.add("origin", w.origin);
        r.add("slug", !w.slug.empty() ? w.slug : !supersedes.empty() ? s.record(supersedes[0]).slug : fresh_slug(s, w.title));
        r.add("title", w.title);
        r.add("content", content.hex());
        r.add("length", decimal(w.text.size()));
        for (const auto t : tags) r.add("tag", s.tax().info(t).qualified);
        for (const auto m : supersedes) r.add("supersedes", s.record(m).key.hex());
        for (const auto m : generalises) r.add("generalises", s.record(m).key.hex());
        for (const auto m : seen) r.add("seen", s.record(m).key.hex());
        for (const auto& c : w.cites) r.add("cite", c);
        if (!w.reason.empty()) r.add("reason", w.reason);
        r.add("author", w.author);
        r.add("session", decimal(w.session));
        r.add("turn", decimal(w.turn));
        if (!w.corpus.empty()) r.add("corpus", w.corpus);
        if (!w.revision.empty()) r.add("revision", w.revision);
        for (std::size_t n = 0; n < w.proposals.size(); ++n) {
            const auto& p = w.proposals[n];
            const std::string k = "proposal." + decimal(n) + ".";
            r.add(k + "concept", p.concept_name);
            r.add(k + "dimension", p.dimension);
            r.add(k + "brief", p.entry.brief);
            r.add(k + "full", p.entry.full);
            r.add(k + "when", p.entry.when);
            r.add(k + "when_not", p.entry.when_not);
            r.add(k + "example", p.entry.example);
        }
        if (w.origin == "merged") out.verdict = verdict(s, supersedes);

        m_store.put_object(content, w.text);  // everything a row names is written before the row (03 §4.1)
        const auto next = append_locked(std::move(r));
        const memory_id id(static_cast<std::uint32_t>(next->size() - 1));
        out.ok = true;
        out.id = id;
        out.key = next->record(id).key;
        out.slug = next->record(id).slug;
        out.version = next->version();
        refresh_view(*next, id, w.text);
        if (!generalises.empty()) write_report(*next, w.session);  // a new generalisation waits in its session's report
        return out;
    }

    /// Several heads that say one thing, joined into one memory that
    /// supersedes them all (Feature 5). Tags default to their union.
    write_outcome merge(const std::vector<std::string>& refs, std::string title, std::string text, std::string reason,
                        std::vector<std::string> tags, std::uint64_t session, std::string author = "agent") {
        write_outcome out;
        const auto snap = current();
        std::vector<memory_id> ms;
        if (!resolve_refs(*snap, refs, ms, out.why)) return out;
        if (ms.size() < 2) {
            out.why = {"merge", "a merge needs at least two memories", {}};
            return out;
        }
        if (tags.empty()) {
            for (const auto m : ms)
                for (const auto t : snap->tags_of(m).members()) {
                    const auto& q = snap->tax().info(tag_id(t)).qualified;
                    if (std::find(tags.begin(), tags.end(), q) == tags.end()) tags.push_back(q);
                }
        }
        write_request w;
        w.title = std::move(title);
        w.text = std::move(text);
        w.reason = std::move(reason);
        w.tags = std::move(tags);
        w.supersedes = refs;
        w.origin = "merged";
        w.session = session;
        w.author = std::move(author);
        return remember(std::move(w));
    }

    // ── learning and curating ─────────────────────────────────────────────

    /// A memory helped (overview §4.4). With a tag it does not carry, the
    /// report counts toward promoting that tag; on a procedure with no tag it
    /// means the steps held. A promotion is a row; the report is not.
    op_outcome learn(std::string_view ref, std::string_view tag, std::string reason, std::uint64_t session) {
        op_outcome out;
        auto snap = current();
        std::vector<memory_id> ms;
        if (!resolve_refs(*snap, {std::string(ref)}, ms, out.why)) return out;
        const memory_id m = ms[0];
        std::optional<tag_id> t;
        if (!tag.empty()) {
            std::vector<tag_id> ts;
            if (!resolve_query_tags(snap->tax(), {std::string(tag)}, ts, out.why)) return out;
            t = ts[0];
        }
        const bool procedure = snap->tax().tag("kind=procedure") && snap->tags_of(m).has(snap->tax().tag("kind=procedure")->value());
        const std::string kind = (!t && procedure) ? "steps_held" : "useful";
        const auto key = snap->record(m).key;
        m_store.append_usage({key, t ? snap->tax().info(*t).qualified : std::string(), kind, m_store.now(), session, reason});
        out.ok = true;
        out.message = "recorded " + kind;
        if (t && !snap->tags_of(m).has(t->value())) {
            std::uint32_t count = 0;
            const auto& q = snap->tax().info(*t).qualified;
            for (const auto& u : m_store.usage())
                if (u.memory == key && u.tag == q && u.kind == "useful") ++count;
            out.message += " — " + decimal(count) + " of " + decimal(m_opts.promotion_threshold) + " toward " + q;
            if (count >= m_opts.promotion_threshold) {
                auto linked = link_impl(std::string(ref), q, "promoted after " + decimal(count) + " useful uses", "learned", "service", decimal(count));
                out.promoted = linked.ok;
                if (linked.ok) out.message += " — promoted";
                out.version = linked.version;
            }
        }
        return out;
    }

    op_outcome link(std::string_view ref, std::string_view tag, std::string reason, std::string author = "human") {
        return link_impl(std::string(ref), std::string(tag), std::move(reason), "curated", std::move(author), {});
    }

    op_outcome unlink(std::string_view ref, std::string_view tag, std::string reason, std::string author = "human") {
        op_outcome out;
        auto guard = m_store.lock();
        catch_up();
        const auto snap = current();
        std::vector<memory_id> ms;
        std::vector<tag_id> ts;
        if (!resolve_refs(*snap, {std::string(ref)}, ms, out.why) || !resolve_query_tags(snap->tax(), {std::string(tag)}, ts, out.why))
            return out;
        const auto inst = snap->instances(ms[0], ts[0]);
        if (inst.empty()) {
            out.why = {"not carried", describe(*snap, ms[0]) + " does not carry " + std::string(tag), {}};
            return out;
        }
        row r;
        r.op = std::string(ops::unlink);
        r.add("memory", snap->record(ms[0]).key.hex());
        r.add("tag", snap->tax().info(ts[0]).qualified);
        for (const auto& i : inst) r.add("removes", i.hex());
        r.add("reason", std::move(reason));
        r.add("author", std::move(author));
        const auto next = append_locked(std::move(r));
        out.ok = true;
        out.version = next->version();
        out.message = "unlinked";
        return out;
    }

    op_outcome retire(std::string_view ref, std::string reason, std::string author = "human") {
        op_outcome out;
        auto guard = m_store.lock();
        catch_up();
        const auto snap = current();
        std::vector<memory_id> ms;
        if (!resolve_refs(*snap, {std::string(ref)}, ms, out.why)) return out;
        if (!snap->is_head(ms[0])) {
            out.why = {"not a head", describe(*snap, ms[0]) + " is already out of retrieval", {}};
            return out;
        }
        row r;
        r.op = std::string(ops::retire);
        r.add("memory", snap->record(ms[0]).key.hex());
        r.add("reason", std::move(reason));
        r.add("author", std::move(author));
        const auto next = append_locked(std::move(r));
        m_store.remove_view(snap->record(ms[0]).slug);
        out.ok = true;
        out.version = next->version();
        out.message = "retired";
        return out;
    }

    // ── consolidation review (overview §4.11) ─────────────────────────────

    /// A human accepts a generalisation: from the next read its instances fold under it. Offered
    /// to the human's tools only — an agent that could accept its own pattern would make the
    /// gate a formality. `message` is the path of the report it refreshed.
    op_outcome accept(std::string_view ref, std::string reason, std::string author = "human") {
        op_outcome out;
        auto guard = m_store.lock();
        catch_up();
        const auto snap = current();
        std::vector<memory_id> ms;
        if (!resolve_refs(*snap, {std::string(ref)}, ms, out.why)) return out;
        const memory_id m = ms[0];
        if (snap->record(m).generalises.empty()) {
            out.why = {"not a generalisation", describe(*snap, m) + " generalises nothing — only a generalisation waits for acceptance", {}};
            return out;
        }
        if (!snap->is_head(m)) {
            out.why = {"not a head", describe(*snap, m) + " has left retrieval — accept the current head", {}};
            for (const auto h : chain_heads(*snap, m)) out.why.facts.push_back(describe(*snap, h));
            return out;
        }
        if (snap->is_accepted(m)) {
            out.why = {"already accepted", describe(*snap, m) + " was accepted before — its instances already fold", {}};
            return out;
        }
        row r;
        r.op = std::string(ops::accept);
        r.add("memory", snap->record(m).key.hex());
        if (!reason.empty()) r.add("reason", std::move(reason));
        r.add("author", std::move(author));
        const auto next = append_locked(std::move(r));
        out.ok = true;
        out.version = next->version();
        out.message = write_report(*next, next->record(m).session);
        return out;
    }

    /// The agent's lessons learnt from a consolidation, kept in the log and published in the
    /// session's report beside what the service can list itself. `message` is the report's path.
    op_outcome record_lessons(std::uint64_t session, std::string text, std::string author = "agent") {
        op_outcome out;
        if (text.empty()) {
            out.why = {"empty", "lessons learnt need text — even \"no pattern, and why\" is a lesson", {}};
            return out;
        }
        auto guard = m_store.lock();
        catch_up();
        row r;
        r.op = std::string(ops::lessons);
        r.add("session", decimal(session));
        r.add("text", std::move(text));
        r.add("author", std::move(author));
        const auto next = append_locked(std::move(r));
        out.ok = true;
        out.version = next->version();
        out.message = write_report(*next, session);
        return out;
    }

    /// The report for one session, as the snapshot stands (overview §4.11): generalisations and
    /// whether each waits for acceptance, where one claims `any`, the cases left, proposals
    /// raised, and the agent's lessons. Everything but the lessons is listed, not judged.
    std::string write_report(const snapshot& s, std::uint64_t session) {
        const auto& tax = s.tax();
        std::vector<memory_id> gens, cases;
        std::vector<row_id> keys;
        for (std::size_t i = 0; i < s.size(); ++i) {
            const memory_id m(static_cast<std::uint32_t>(i));
            if (s.record(m).session != session) continue;
            keys.push_back(s.record(m).key);
            if (!s.is_head(m)) continue;
            (s.record(m).generalises.empty() ? cases : gens).push_back(m);
        }
        std::string out = "---\nsession: " + decimal(session) + "\nversion: " + decimal(s.version()) + "\n---\n\n";
        out += "# Lessons learnt — session " + decimal(session) + "\n\n";
        out += "Regenerated from the audit log whenever this session's consolidation changes. Act with the "
               "commands below rather than by editing this file.\n\n## Generalisations\n\n";
        if (gens.empty()) out += "None written.\n\n";
        for (const auto g : gens) {
            const auto& rec = s.record(g);
            out += "### " + describe(s, g) + "\n\n";
            if (s.is_accepted(g)) {
                out += "**Accepted** — its instances fold under it in reads.\n\n";
            } else {
                out += "**Waiting for your acceptance** — until then its instances are placed on their own.\n\n"
                       "Accept it: `oo memory accept " + rec.key.hex().substr(0, 12) + " --reason \"…\"`\n\n";
            }
            out += "Generalises:\n\n";
            for (const auto& k : rec.generalises)
                if (const auto x = s.find(k)) out += "- " + describe(s, *x) + "\n";
            std::vector<std::string> any;
            for (const auto t : s.tags_of(g).members())
                if (tax.info(tag_id(t)).any) any.push_back("`" + tax.info(tag_id(t)).qualified + "`");
            if (!any.empty())
                out += "\nClaims every value of " + join(any, ", ") +
                       " — the coverage check cannot see overreach, so check that its cases reach that far.\n";
            out += "\n";
        }
        out += "## Left as cases\n\n";
        std::size_t left = 0;
        for (const auto c : cases)
            if (s.generalised_by(c).empty()) {
                out += "- " + describe(s, c) + "\n";
                ++left;
            }
        out += left ? "\n" : "None — every memory this session wrote belongs to a pattern.\n\n";
        out += "## Vocabulary proposals raised\n\n";
        std::size_t raised = 0;
        for (const auto& p : s.proposals) {
            if (std::none_of(p.asked_by.begin(), p.asked_by.end(),
                             [&](const row_id& k) { return std::find(keys.begin(), keys.end(), k) != keys.end(); }))
                continue;
            out += "- " + (p.dimension.empty() ? std::string("(new dimension)") : p.dimension) + "=" + p.concept_name +
                   " — " + p.entry.brief + "\n";
            ++raised;
        }
        out += raised ? "\n" : "None.\n\n";
        out += "## Lessons learnt\n\n";
        std::size_t told = 0;
        for (const auto& l : s.lessons) {
            if (l.session != session) continue;
            out += "### " + l.time + " — " + l.author + "\n\n" + l.text + "\n\n";
            ++told;
        }
        if (!told) out += "None recorded yet — a consolidation ends by recording them.\n";
        return m_store.write_report(session, out);
    }

    /// Hand-written corpus blocks, reconciled by slug and digest (Feature 4):
    /// an unknown slug is a new chain, a known slug with new text supersedes
    /// its head, an unchanged block is nothing. Seed writes have no look step.
    ingest_outcome ingest(const std::vector<corpus_block>& blocks, const std::string& corpus, const std::string& revision,
                          const std::string& corpus_name) {
        ingest_outcome out;
        for (const auto& b : blocks) {
            const auto snap = current();
            std::optional<memory_id> head;
            for (std::size_t i = 0; i < snap->size(); ++i) {
                const memory_id m(static_cast<std::uint32_t>(i));
                if (snap->is_head(m) && snap->record(m).slug == b.slug) head = m;
            }
            if (head && snap->record(*head).content == digest::of(b.text)) {
                ++out.unchanged;
                continue;
            }
            write_request w;
            w.title = b.title;
            w.text = b.text;
            w.tags = b.tags;
            w.slug = b.slug;
            w.origin = "seed";
            w.author = "ingest";
            w.corpus = corpus;
            w.revision = revision;
            w.reason = head ? corpus + " changed" : "ingested from " + corpus;
            if (head) w.supersedes = {"#" + decimal(head->value())};
            const auto r = remember(std::move(w));
            if (!r.ok) {
                std::string line = corpus_name + ":" + decimal(b.line) + ": " + b.slug + ": " + r.why.message;
                for (const auto& f : r.why.facts) line += " · " + f;
                out.refused.push_back(std::move(line));
                continue;
            }
            head ? ++out.superseded : ++out.added;
        }
        return out;
    }

    // ── looking things up ─────────────────────────────────────────────────

    /// A memory reference — `#12`, or a key or a prefix of at least 8 hex
    /// characters — resolved in the current snapshot.
    [[nodiscard]] std::optional<memory_id> resolve(std::string_view ref) const {
        std::vector<memory_id> ms;
        refusal why;
        if (!resolve_refs(*current(), {std::string(ref)}, ms, why)) return std::nullopt;
        return ms[0];
    }

    [[nodiscard]] std::optional<std::string> text_of(const snapshot& s, memory_id m) const {
        return m_store.object(s.record(m).content);
    }

    [[nodiscard]] counters counts(const row_id& key) const {
        counters c;
        for (const auto& u : m_store.usage()) {
            if (u.memory != key) continue;
            if (u.kind == "included") ++c.included;
            else if (u.kind == "admitted") ++c.admitted;
            else if (u.kind == "useful" || u.kind == "steps_held") ++c.useful;
        }
        return c;
    }

    [[nodiscard]] std::string describe(const snapshot& s, memory_id m) const {
        return "#" + decimal(m.value()) + " " + s.record(m).key.hex().substr(0, 8) + " " + s.record(m).title;
    }

private:
    // ── replay and publication ────────────────────────────────────────────

    void publish(std::shared_ptr<const snapshot> s) {
        std::lock_guard<std::mutex> g(m_publish);
        m_current = std::move(s);
    }

    /// Rows in topological order, ties by row id (03 §5): the same set of
    /// rows replays the same way whatever order the files were read in.
    /// With `upto`, only that row and its ancestors.
    [[nodiscard]] snapshot replay(const std::vector<row>& rows, std::optional<row_id> upto,
                                  std::shared_ptr<const taxonomy> tax = nullptr) const {
        snapshot s(tax ? std::move(tax) : m_tax);
        std::unordered_map<row_id, const row*, digest_hash> by_id;
        for (const auto& r : rows) by_id.emplace(r.id, &r);  // a row present in two files is one row
        std::unordered_set<row_id, digest_hash> keep;
        if (upto) {
            std::vector<row_id> stack{*upto};
            while (!stack.empty()) {
                const row_id id = stack.back();
                stack.pop_back();
                if (!keep.insert(id).second) continue;
                if (auto it = by_id.find(id); it != by_id.end())
                    for (const auto& p : it->second->parents) stack.push_back(p);
            }
        }
        std::unordered_map<row_id, std::vector<const row*>, digest_hash> children;
        std::unordered_map<row_id, std::size_t, digest_hash> waiting;
        auto cmp = [](const row* a, const row* b) { return b->id < a->id; };
        std::priority_queue<const row*, std::vector<const row*>, decltype(cmp)> ready(cmp);
        for (const auto& r : rows) {
            if (upto && !keep.count(r.id)) continue;
            if (by_id.at(r.id) != &r) continue;  // a duplicate of a row already queued
            std::size_t n = 0;
            for (const auto& p : r.parents)
                if (by_id.count(p) && (!upto || keep.count(p))) {
                    children[p].push_back(&r);
                    ++n;
                }
            waiting[r.id] = n;
            if (n == 0) ready.push(&r);
        }
        while (!ready.empty()) {
            const row* r = ready.top();
            ready.pop();
            s.apply(*r);
            for (const row* c : children[r->id])
                if (--waiting[c->id] == 0) ready.push(c);
        }
        drop_settled_proposals(s);
        return s;
    }

    [[nodiscard]] static std::vector<row_id> head_rows(const std::vector<row>& rows) {
        std::unordered_set<row_id, digest_hash> parents;
        for (const auto& r : rows)
            for (const auto& p : r.parents) parents.insert(p);
        std::vector<row_id> out;
        for (const auto& r : rows)
            if (!parents.count(r.id)) out.push_back(r.id);
        std::sort(out.begin(), out.end());
        return out;
    }

    [[nodiscard]] static digest last_taxonomy_version(const std::vector<row>& rows) {
        // the latest by time among taxonomy rows; replay order is not needed for one field
        const row* best = nullptr;
        for (const auto& r : rows)
            if (r.op == ops::taxonomy && (!best || r.time > best->time || (r.time == best->time && best->id < r.id))) best = &r;
        return best ? digest::from_hex(best->get("version")).value_or(digest{}) : digest{};
    }

    /// Folds in rows another process appended. Holds the lock (callers take it).
    void catch_up() {
        std::vector<row> more = m_store.new_rows();
        std::erase_if(more, [&](const row& r) { return !m_applied.insert(r.id).second; });
        if (more.empty()) return;
        auto next = std::make_shared<snapshot>(*current());
        for (const auto& r : more) next->apply(r);
        drop_settled_proposals(*next);
        publish(std::move(next));
    }

    /// Seals `r` onto the current head and appends it — the commit point —
    /// then publishes the snapshot that includes it. Holds the lock.
    std::shared_ptr<const snapshot> append_locked(row r) {
        const auto snap = current();
        if (r.parents.empty() && snap->version() > 0) r.parents = {snap->head()};
        if (r.op != ops::merge) {
            r.clone = m_store.clone();
            r.seq = m_store.next_seq();
            r.time = m_store.now();
        }  // a merge row is a function of its parents alone: whoever merges, it is the same row (03 §5)
        seal(r);
        if (m_applied.count(r.id)) return snap;
        m_store.append(r);
        m_applied.insert(r.id);
        auto next = std::make_shared<snapshot>(*snap);
        next->apply(r);
        drop_settled_proposals(*next);
        std::shared_ptr<const snapshot> out = next;
        publish(out);
        return out;
    }

    /// A proposal whose concept is now a value of its dimension was accepted
    /// (by a human editing the pack file); one whose concept is listed as
    /// rejected is settled too. Neither is pending any more.
    void drop_settled_proposals(snapshot& s) const {
        const auto& tax = s.tax();
        std::erase_if(s.proposals, [&](const pending_proposal& p) {
            if (accepted_tag(tax, p)) return true;
            const auto k = pending_proposal::key(p.concept_name, "");
            return std::any_of(tax.rejections().begin(), tax.rejections().end(),
                               [&](const rejection& r) { return pending_proposal::key(r.concept_name, "") == k; });
        });
    }

    [[nodiscard]] static std::optional<tag_id> accepted_tag(const taxonomy& tax, const pending_proposal& p) {
        if (p.dimension.empty()) return std::nullopt;
        std::string value;
        for (const char c : p.concept_name) value.push_back(c == ' ' ? '_' : static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        return tax.tag(p.dimension + "=" + value);
    }

    /// Accepting a proposal links every memory that asked for it, with source
    /// `proposed` (02 §3.1). Runs at open, holding the lock.
    void link_accepted_proposals() {
        std::vector<row> rows = m_store.rows();
        // Fold without dropping settled proposals, to see which were accepted.
        snapshot folded(m_tax);
        for (const auto& r : rows) folded.apply(r);
        for (const auto& p : folded.proposals) {
            const auto t = accepted_tag(*m_tax, p);
            if (!t) continue;
            for (const auto& asker : p.asked_by) {
                const auto snap = current();
                const auto m = snap->find(asker);
                if (!m || !snap->is_head(*m) || snap->tags_of(*m).has(t->value())) continue;
                row r;
                r.op = std::string(ops::link);
                r.add("memory", asker.hex());
                r.add("tag", m_tax->info(*t).qualified);
                r.add("source", "proposed");
                r.add("reason", "accepted into the vocabulary");
                r.add("author", "service");
                append_locked(std::move(r));
            }
        }
    }

    op_outcome link_impl(std::string ref, std::string tag, std::string reason, std::string source, std::string author,
                         std::string evidence) {
        op_outcome out;
        auto guard = m_store.lock();
        catch_up();
        const auto snap = current();
        std::vector<memory_id> ms;
        std::vector<tag_id> ts;
        if (!resolve_refs(*snap, {ref}, ms, out.why) || !resolve_query_tags(snap->tax(), {tag}, ts, out.why)) return out;
        if (!snap->is_head(ms[0])) {
            out.why = {"not a head", describe(*snap, ms[0]) + " has been superseded — link its current head", {}};
            return out;
        }
        if (snap->tags_of(ms[0]).has(ts[0].value())) {
            out.why = {"already carried", describe(*snap, ms[0]) + " already carries " + tag, {}};
            return out;
        }
        row r;
        r.op = std::string(ops::link);
        r.add("memory", snap->record(ms[0]).key.hex());
        r.add("tag", snap->tax().info(ts[0]).qualified);
        r.add("source", std::move(source));
        r.add("reason", std::move(reason));
        r.add("author", std::move(author));
        if (!evidence.empty()) r.add("evidence", std::move(evidence));
        const auto next = append_locked(std::move(r));
        out.ok = true;
        out.version = next->version();
        out.message = "linked";
        return out;
    }

    // ── names ─────────────────────────────────────────────────────────────

    static bool resolve_query_tags(const taxonomy& tax, const std::vector<std::string>& names, std::vector<tag_id>& out,
                                   refusal& why) {
        for (const auto& n : names) {
            const auto t = tax.tag(n);
            if (!t) {
                why = {"unknown tag", n + " is not in the vocabulary", tax.suggestions(n)};
                return false;
            }
            if (tax.info(*t).retired) {
                why = {"retired", n + " is retired" + (tax.info(*t).see.empty() ? "" : " — see " + tax.info(*t).see), {}};
                return false;
            }
            if (tax.info(*t).any) {
                why = {"any in a query", n + ": `any` is for memories that apply to every value, not for queries", {}};
                return false;
            }
            if (std::find(out.begin(), out.end(), *t) == out.end()) out.push_back(*t);
        }
        return true;
    }

    static bool resolve_write_tags(const snapshot& s, const write_request& w, std::vector<tag_id>& out, refusal& why) {
        const auto& tax = s.tax();
        std::vector<std::string> domains;
        for (const auto& n : w.tags) {
            const auto t = tax.tag(n);
            if (!t) {
                why = {"unknown tag", n + " is not in the vocabulary — propose it instead of forcing a fit", tax.suggestions(n)};
                return false;
            }
            if (tax.info(*t).retired) {
                why = {"retired", n + " is retired" + (tax.info(*t).see.empty() ? "" : " — see " + tax.info(*t).see), {}};
                return false;
            }
            if (std::find(out.begin(), out.end(), *t) == out.end()) out.push_back(*t);
            if (tax.info(tax.dimension_of(*t)).name == "domain" && !tax.info(*t).any) domains.push_back(tax.info(*t).value);
        }
        for (const auto d : tax.hard_dimensions_for(domains)) {
            const bool answered = std::any_of(out.begin(), out.end(), [&](tag_id t) { return tax.dimension_of(t) == d; });
            if (!answered) {
                why = {"unanswered", "a memory must answer every hard question, or no read can find it: " + tax.info(d).name +
                                         " — use " + tax.info(d).name + "=any if it holds for every value",
                       tax.suggestions(tax.info(d).name + "=")};
                return false;
            }
        }
        return true;
    }

    /// The space a new chain lands in: the write's hard-dimension tags, with an
    /// `any` widened to every value of its dimension.
    [[nodiscard]] static std::vector<tag_id> write_space(const snapshot& s, const std::vector<tag_id>& tags) {
        const auto& tax = s.tax();
        std::vector<tag_id> out;
        for (const auto t : tags) {
            const auto d = tax.dimension_of(t);
            if (tax.default_role(d) != role::hard) continue;
            if (tax.info(t).any) {
                for (const auto v : tax.values_of(d)) out.push_back(v);
            } else {
                out.push_back(t);
            }
        }
        return out;
    }

    /// What the service can check of a generalisation without reading it (overview §4.11): at
    /// least two instances, each a head, none retired by this same write, and every value an
    /// instance gives a hard dimension also given by the generalisation — or `any` — so it is
    /// findable wherever one of its cases is. Whether the pattern is real is not checkable here.
    bool check_generalisation(const snapshot& s, const std::vector<tag_id>& tags, const std::vector<memory_id>& supersedes,
                              const std::vector<memory_id>& generalises, refusal& why) const {
        if (generalises.size() < 2) {
            why = {"one case", "a generalisation states what several memories share — name at least two instances", {}};
            return false;
        }
        for (const auto m : generalises) {
            if (!s.is_head(m)) {
                why = {"not a head", "#" + decimal(m.value()) + " has been superseded — generalise the current head", {}};
                for (const auto h : chain_heads(s, m)) why.facts.push_back(describe(s, h));
                return false;
            }
            if (std::find(supersedes.begin(), supersedes.end(), m) != supersedes.end()) {
                why = {"evidence", "#" + decimal(m.value()) + " cannot be both superseded and generalised — a generalisation keeps its evidence", {}};
                return false;
            }
        }
        const auto& tax = s.tax();
        tag_set mine;
        for (const auto t : tags) mine.note(t.value());
        std::vector<std::string> missing;
        for (std::size_t i = 0; i < tax.dimensions(); ++i) {
            const dimension_id d(static_cast<dimension_id::value_type>(i));
            if (tax.default_role(d) != role::hard) continue;
            if (const auto any = tax.any_of(d); any && mine.has(any->value())) continue;
            for (const auto m : generalises)
                for (const auto t : s.tags_of(m).members())
                    if (tax.dimension_of(tag_id(t)) == d && !mine.has(t))
                        missing.push_back(describe(s, m) + " answers " + tax.info(tag_id(t)).qualified);
        }
        if (!missing.empty()) {
            why = {"not covered", "a generalisation must be findable wherever its cases are — add these values, or the dimension's `any` if the pattern holds for every value",
                   std::move(missing)};
            return false;
        }
        return true;
    }

    bool resolve_refs(const snapshot& s, const std::vector<std::string>& refs, std::vector<memory_id>& out, refusal& why) const {
        for (const auto& ref : refs) {
            std::optional<memory_id> m;
            if (!ref.empty() && ref[0] == '#') {
                std::uint64_t n = 0;
                bool digits = ref.size() > 1;
                for (std::size_t i = 1; i < ref.size(); ++i) {
                    if (ref[i] < '0' || ref[i] > '9') digits = false;
                    else n = n * 10 + static_cast<std::uint64_t>(ref[i] - '0');
                }
                if (digits && n < s.size()) m = memory_id(static_cast<std::uint32_t>(n));
            } else if (ref.size() >= 8) {
                std::vector<memory_id> hits;
                for (std::size_t i = 0; i < s.size(); ++i)
                    if (s.record(memory_id(static_cast<std::uint32_t>(i))).key.hex().starts_with(ref))
                        hits.push_back(memory_id(static_cast<std::uint32_t>(i)));
                if (hits.size() > 1) {
                    why = {"ambiguous", ref + " matches " + decimal(hits.size()) + " memories — use more of the key", {}};
                    return false;
                }
                if (!hits.empty()) m = hits[0];
            }
            if (!m) {
                why = {"unknown memory", ref + " names no memory — use #n from a read, or a key of at least 8 characters", {}};
                return false;
            }
            if (std::find(out.begin(), out.end(), *m) == out.end()) out.push_back(*m);
        }
        return true;
    }

    [[nodiscard]] static std::vector<memory_id> chain_heads(const snapshot& s, memory_id m) {
        std::vector<memory_id> out, stack{m};
        while (!stack.empty()) {
            const auto x = stack.back();
            stack.pop_back();
            if (s.is_head(x)) {
                if (std::find(out.begin(), out.end(), x) == out.end()) out.push_back(x);
                continue;
            }
            for (const auto y : s.superseded_by(x)) stack.push_back(y);
        }
        return out;
    }

    [[nodiscard]] static std::string fresh_slug(const snapshot& s, std::string_view title) {
        std::string base;
        for (const char c : title) {
            const auto u = static_cast<unsigned char>(c);
            if (std::isalnum(u)) base.push_back(static_cast<char>(std::tolower(u)));
            else if (!base.empty() && base.back() != '-') base.push_back('-');
            if (base.size() >= 60) break;
        }
        while (!base.empty() && base.back() == '-') base.pop_back();
        if (base.empty()) base = "memory";
        std::string slug = base;
        for (std::uint32_t n = 2; s.slug_taken(slug); ++n) slug = base + "-" + decimal(n);
        return slug;
    }

    /// Which kind of recollection failure a merge records (overview §4.7): the
    /// later of two sources either came through ingestion (no read expected),
    /// saw the earlier and wrote anyway (judgement), or never had it in its
    /// space (classification mismatch).
    [[nodiscard]] static std::string verdict(const snapshot& s, const std::vector<memory_id>& ms) {
        std::vector<memory_id> sorted = ms;
        std::sort(sorted.begin(), sorted.end());
        const auto& later = s.record(sorted.back());
        const auto& earlier = s.record(sorted.front());
        if (later.origin == "seed") return "ingested — the hand-written door has no look step, so no read was expected";
        if (std::find(later.seen.begin(), later.seen.end(), earlier.key) != later.seen.end())
            return "judgement error — #" + decimal(sorted.back().value()) + " was written after reading #" + decimal(sorted.front().value());
        return "classification mismatch — #" + decimal(sorted.front().value()) + " was not in the space #" +
               decimal(sorted.back().value()) + " read; review the descriptions of the tags that differ";
    }

    void refresh_view(const snapshot& s, memory_id m, std::string_view text) {
        std::vector<std::string> names;
        for (const auto t : s.tags_of(m).members()) names.push_back(s.tax().info(tag_id(t)).qualified);
        m_store.write_view(s.record(m), text, names);
    }

    static std::string join(const std::vector<std::string>& v, std::string_view sep) {
        std::string out;
        for (std::size_t i = 0; i < v.size(); ++i) {
            if (i) out.append(sep);
            out.append(v[i]);
        }
        return out;
    }

    Store& m_store;
    service_options m_opts;
    std::shared_ptr<const taxonomy> m_tax;
    mutable std::mutex m_publish;
    std::shared_ptr<const snapshot> m_current;
    std::vector<review_item> m_load_reviews;
    std::unordered_set<row_id, digest_hash> m_applied;  // every row id in the published snapshot
};

}  // namespace pygim::memory
