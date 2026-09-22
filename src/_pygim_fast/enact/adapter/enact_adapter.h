// memory/adapter/enact_adapter.h — the Python face of the memory service.
//
// One class, `pygim.enact.Enact`, over the files store. Arguments are
// converted to C++ before the GIL is released; results come back as plain
// dicts and lists, ready to be handed to an agent as JSON. A refused write is a
// result ({"ok": False, "refused": …, "facts": […]}), not an exception: a
// refusal is information the caller acts on (overview §4.5).
#pragma once

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "../core/service.h"
#include "../strategy/files/corpus.h"
#include "../strategy/files/store.h"

namespace pygim::enact::adapter {

namespace py = pybind11;
namespace F = pygim::enact::strategy::files;
using service = memory_service<F::store>;

inline std::string ref(memory_id m) { return "#" + decimal(m.value()); }
/// Keys a record names, as `#n` in this snapshot; a key the snapshot does not hold is left out.
inline std::vector<std::string> refs_of(const snapshot& s, const std::vector<row_id>& keys) {
    std::vector<std::string> out;
    for (const auto& k : keys)
        if (auto m = s.find(k)) out.push_back(ref(*m));
    return out;
}
inline std::string score_text(score_t milli) { return milli % 1000 == 0 ? decimal(static_cast<std::uint64_t>(milli / 1000)) + ".0"
                                                                         : weight_text(static_cast<weight_t>(milli)); }

inline py::dict refused(const refusal& why) {
    py::dict d;
    d["ok"] = false;
    d["refused"] = why.kind;
    d["message"] = why.message;
    d["facts"] = why.facts;
    return d;
}

inline std::vector<std::string> names(const taxonomy& tax, const std::vector<tag_id>& ts) {
    std::vector<std::string> out;
    for (const auto t : ts) out.push_back(tax.info(t).qualified);
    return out;
}

inline py::dict entry_dict(const codebook_entry& e) {
    py::dict d;
    d["brief"] = e.brief;
    if (!e.full.empty()) d["full"] = e.full;
    d["when"] = e.when;
    d["when_not"] = e.when_not;
    d["example"] = e.example;
    return d;
}

class Enact {
public:
    explicit Enact(const std::string& root) : m_store(std::make_unique<F::store>(root)), m_service(std::make_unique<service>(*m_store)) {
        py::gil_scoped_release nogil;
        m_service->open();
    }

    static void init(const std::string& root) { F::store::init(root); }

    [[nodiscard]] std::string root() const { return m_store->root().string(); }
    [[nodiscard]] std::uint64_t version() const { return m_service->current()->version(); }
    [[nodiscard]] std::string head() const { return m_service->current()->head().hex(); }
    [[nodiscard]] std::string taxonomy_version() const { return m_service->current()->tax().version().hex(); }

    void refresh() {
        py::gil_scoped_release nogil;
        m_service->refresh();
    }

    /// Opens a session: its number, where the store stands, and everything
    /// waiting for judgement (00a, Scenario 0, Panel 7).
    py::dict session() {
        std::uint64_t n;
        {
            py::gil_scoped_release nogil;
            m_service->refresh();
            n = m_service->new_session();
        }
        const auto s = m_service->current();
        py::dict d;
        d["session"] = n;
        d["clone"] = m_store->clone();
        d["version"] = s->version();
        d["head"] = s->head().hex();
        d["taxonomy"] = s->tax().version().hex();
        d["memories"] = s->heads();
        py::list reviews;
        for (const auto& r : m_service->reviews()) reviews.append(py::dict(py::arg("kind") = r.kind, py::arg("text") = r.text));
        for (const auto& p : m_store->problems()) reviews.append(py::dict(py::arg("kind") = "file", py::arg("text") = p));
        d["reviews"] = reviews;
        d["proposals"] = proposals();
        d["mailbox"] = mailbox(false, {});   // what other sessions left here, still open
        d["waiting_acceptance"] = waiting_acceptance().size();
        return d;
    }

    /// The vocabulary the agent classifies against: every live dimension and
    /// value with its codebook entry, and the rejections (02 §3.2).
    py::dict vocabulary() const {
        const auto s = m_service->current();
        const auto& tax = s->tax();
        py::dict d;
        d["version"] = tax.version().hex();
        py::list dims;
        for (std::size_t i = 0; i < tax.dimensions(); ++i) {
            const dimension_id di(static_cast<dimension_id::value_type>(i));
            const auto& info = tax.info(di);
            py::dict dd;
            dd["name"] = info.name;
            dd["role"] = std::string(role_name(info.default_role));
            dd["weight"] = weight_text(info.weight);
            dd["pack"] = info.pack;
            dd["entry"] = entry_dict(info.entry);
            py::list values;
            for (const auto t : tax.values_of(di)) {
                const auto& ti = tax.info(t);
                if (ti.retired) continue;
                py::dict vd;
                vd["tag"] = ti.qualified;
                vd["entry"] = entry_dict(ti.entry);
                if (ti.any) vd["any"] = true;
                if (ti.source) vd["source"] = ti.source->doc + ":L" + decimal(ti.source->line);
                values.append(vd);
            }
            dd["values"] = values;
            dims.append(dd);
        }
        d["dimensions"] = dims;
        py::list rejected;
        for (const auto& r : tax.rejections()) {
            py::dict rd;
            rd["concept"] = r.concept_name;
            rd["reason"] = r.reason;
            if (!r.see.empty()) rd["see"] = r.see;
            rejected.append(rd);
        }
        d["rejected"] = rejected;
        return d;
    }

    py::dict read(const std::vector<std::string>& hard, const std::vector<std::string>& soft, std::uint32_t max_memories,
                  std::uint32_t budget, std::string term, std::uint64_t session) {
        query q;
        q.hard = hard;
        q.soft = soft;
        q.max_memories = max_memories;
        q.budget = budget;
        q.term = std::move(term);
        read_outcome out;
        {
            py::gil_scoped_release nogil;
            m_service->refresh();
            out = m_service->read(q, session);
        }
        return read_dict(out);
    }

    py::dict rerun(const py::dict& rec) {
        receipt r;
        const auto snap = digest::from_hex(rec["snapshot"].cast<std::string>());
        const auto tax = digest::from_hex(rec["taxonomy"].cast<std::string>());
        if (!snap || !tax) throw py::value_error("a receipt names its snapshot and taxonomy by 32-character digests");
        r.snapshot = *snap;
        r.taxonomy = *tax;
        r.asked.hard = rec["hard"].cast<std::vector<std::string>>();
        r.asked.soft = rec["soft"].cast<std::vector<std::string>>();
        r.asked.max_memories = rec.contains("max") ? rec["max"].cast<std::uint32_t>() : 8;
        r.asked.budget = rec.contains("budget") ? rec["budget"].cast<std::uint32_t>() : 0;
        r.asked.term = rec.contains("term") ? rec["term"].cast<std::string>() : std::string();
        read_outcome out;
        {
            py::gil_scoped_release nogil;
            out = m_service->rerun(r);
        }
        py::dict d = read_dict(out, false);
        if (out.ok && rec.contains("keys")) {
            std::vector<std::string> before = rec["keys"].cast<std::vector<std::string>>();
            std::vector<std::string> now;
            for (const auto& k : out.rec.keys) now.push_back(k.hex());
            d["same"] = before == now;
        }
        return d;
    }

    py::dict remember(std::string title, std::string text, std::vector<std::string> tags, std::string reason,
                      std::vector<std::string> supersedes, std::vector<std::string> generalises, std::vector<std::string> seen,
                      std::vector<std::string> cites, const py::list& proposals, std::uint64_t session, std::uint32_t turn,
                      std::string author, std::string origin) {
        if (origin != "written" && origin != "seed")
            return refused({"origin", "origin is `written`, or `seed` when seeding a store from existing documents — not " + origin, {}});
        write_request w;
        w.origin = std::move(origin);
        w.title = std::move(title);
        w.text = std::move(text);
        w.tags = std::move(tags);
        w.reason = std::move(reason);
        w.supersedes = std::move(supersedes);
        w.generalises = std::move(generalises);
        w.seen = std::move(seen);
        w.cites = std::move(cites);
        w.session = session;
        w.turn = turn;
        w.author = std::move(author);
        for (const auto& item : proposals) {
            const auto p = item.cast<py::dict>();
            proposal_request pr;
            pr.concept_name = p.contains("concept") ? p["concept"].cast<std::string>() : std::string();
            pr.dimension = p.contains("dimension") ? p["dimension"].cast<std::string>() : std::string();
            auto get = [&](const char* k) { return p.contains(k) ? p[k].cast<std::string>() : std::string(); };
            pr.entry = {get("brief"), get("full"), get("when"), get("when_not"), get("example")};
            w.proposals.push_back(std::move(pr));
        }
        write_outcome out;
        {
            py::gil_scoped_release nogil;
            out = m_service->remember(std::move(w));
        }
        return write_dict(out);
    }

    py::dict merge(const std::vector<std::string>& memories, std::string title, std::string text, std::string reason,
                   std::vector<std::string> tags, std::uint64_t session, std::string author) {
        write_outcome out;
        {
            py::gil_scoped_release nogil;
            out = m_service->merge(memories, std::move(title), std::move(text), std::move(reason), std::move(tags), session, std::move(author));
        }
        return write_dict(out);
    }

    py::dict learn(const std::string& memory, const std::string& tag, std::string reason, std::uint64_t session,
                   const std::string& verdict) {
        op_outcome out;
        {
            py::gil_scoped_release nogil;
            out = m_service->learn(memory, tag, std::move(reason), session, verdict);
        }
        py::dict d = op_dict(out);
        d["promoted"] = out.promoted;
        return d;
    }

    py::dict link(const std::string& memory, const std::string& tag, std::string reason, std::string author) {
        op_outcome out;
        {
            py::gil_scoped_release nogil;
            out = m_service->link(memory, tag, std::move(reason), std::move(author));
        }
        return op_dict(out);
    }

    py::dict unlink(const std::string& memory, const std::string& tag, std::string reason, std::string author) {
        op_outcome out;
        {
            py::gil_scoped_release nogil;
            out = m_service->unlink(memory, tag, std::move(reason), std::move(author));
        }
        return op_dict(out);
    }

    py::dict cite(const std::string& memory, const std::string& locator, std::string reason, std::string author) {
        op_outcome out;
        {
            py::gil_scoped_release nogil;
            out = m_service->cite(memory, locator, std::move(reason), std::move(author));
        }
        return op_dict(out);
    }

    py::dict uncite(const std::string& memory, const std::string& locator, std::string reason, std::string author) {
        op_outcome out;
        {
            py::gil_scoped_release nogil;
            out = m_service->uncite(memory, locator, std::move(reason), std::move(author));
        }
        return op_dict(out);
    }

    py::dict retire(const std::string& memory, std::string reason, std::string author) {
        op_outcome out;
        {
            py::gil_scoped_release nogil;
            out = m_service->retire(memory, std::move(reason), std::move(author));
        }
        return op_dict(out);
    }

    /// Every head carrying any of `tags`, in id order, without recording a read — no receipt, no
    /// usage — so a server can hand each session its standing knowledge (preferences, procedures)
    /// without inflating a counter. A tag the vocabulary lacks is skipped: that store has none of it.
    py::list heads(const std::vector<std::string>& tags) {
        {
            py::gil_scoped_release nogil;
            m_service->refresh();
        }
        const auto snap = m_service->current();
        std::vector<std::uint32_t> ids;
        for (const auto& name : tags)
            if (const auto t = snap->tax().tag(name))
                for (const auto m : snap->carrying(*t).members())
                    if (snap->is_head(memory_id(m))) ids.push_back(m);
        std::sort(ids.begin(), ids.end());
        ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
        py::list out;
        for (const auto id : ids) {
            const memory_id m(id);
            const auto& r = snap->record(m);
            py::dict d;
            d["memory"] = ref(m);
            d["key"] = r.key.hex().substr(0, 12);
            d["title"] = r.title;
            d["text"] = m_service->text_of(*snap, m).value_or("");
            d["tokens"] = r.tokens;
            std::vector<std::string> names;
            for (const auto t : snap->tags_of(m).members()) names.push_back(snap->tax().info(tag_id(t)).qualified);
            d["tags"] = names;
            out.append(d);
        }
        return out;
    }

    /// Leaves a message in this store's mailbox for whoever works here next.
    py::dict post(std::string text, std::string kind, std::string to, std::string about, std::string reply_to,
                  std::string resolves, std::uint64_t session, std::string author) {
        mailbox_entry m;
        m.text = std::move(text);
        m.kind = std::move(kind);
        m.to = std::move(to);
        m.about = std::move(about);
        m.reply_to = std::move(reply_to);
        m.resolves = std::move(resolves);
        m.session = session;
        m.author = std::move(author);
        op_outcome out;
        {
            py::gil_scoped_release nogil;
            out = m_service->post(std::move(m));
        }
        if (!out.ok) return refused(out.why);
        py::dict d;
        d["ok"] = true;
        d["message"] = out.message;   // the new message's id
        d["waiting"] = m_service->waiting();
        return d;
    }

    /// The mailbox: what is still open, oldest first, or everything with `all`.
    py::list mailbox(bool all, const std::string& mine) {
        py::list out;
        for (const auto& m : m_service->mailbox(all, mine)) {
            py::dict d;
            d["id"] = m.id;
            d["kind"] = m.kind;
            d["text"] = m.text;
            d["author"] = m.author;
            d["time"] = m.time;
            d["session"] = m.session;
            d["seq"] = m.seq;
            if (!m.to.empty()) d["to"] = m.to;
            if (!m.about.empty()) d["about"] = m.about;
            if (!m.reply_to.empty()) d["reply_to"] = m.reply_to;
            if (!m.resolves.empty()) d["resolves"] = m.resolves;
            out.append(d);
        }
        return out;
    }

    /// Everything waiting for a person to accept it, with what each would fold. The gate is the one
    /// step no agent may take, so it must be the most readable thing here — a key alone tells a
    /// reader nothing about what they are approving.
    py::list waiting_acceptance() {
        const auto snap = m_service->current();
        py::list out;
        for (std::size_t i = 0; i < snap->size(); ++i) {
            const memory_id m(static_cast<std::uint32_t>(i));
            const auto& r = snap->record(m);
            if (r.generalises.empty() || !snap->is_head(m) || snap->is_accepted(m)) continue;
            py::dict d;
            d["memory"] = ref(m);
            d["key"] = r.key.hex().substr(0, 12);
            d["title"] = r.title;
            d["text"] = m_service->text_of(*snap, m).value_or("");
            d["session"] = r.session;
            std::vector<std::string> tags;
            for (const auto t : snap->tags_of(m).members()) tags.push_back(snap->tax().info(tag_id(t)).qualified);
            d["tags"] = tags;
            py::list folds;
            for (const auto& k : r.generalises) {
                const auto x = snap->find(k);
                if (!x) continue;
                folds.append(py::dict(py::arg("memory") = ref(*x), py::arg("key") = snap->record(*x).key.hex().substr(0, 12),
                                      py::arg("title") = snap->record(*x).title));
            }
            d["folds"] = folds;
            out.append(d);
        }
        return out;
    }

    /// A human accepts a generalisation; its instances fold from the next read. The report of
    /// the session that wrote it is refreshed, and its path returned as `report`.
    py::dict accept(const std::string& memory, std::string reason, std::string author) {
        op_outcome out;
        {
            py::gil_scoped_release nogil;
            out = m_service->accept(memory, std::move(reason), std::move(author));
        }
        py::dict d = op_dict(out);
        if (out.ok) d["report"] = out.message;
        return d;
    }

    /// The agent's lessons learnt from a consolidation, published in the session's report.
    py::dict lessons(std::uint64_t session, std::string text, std::string author) {
        op_outcome out;
        {
            py::gil_scoped_release nogil;
            out = m_service->record_lessons(session, std::move(text), std::move(author));
        }
        py::dict d = op_dict(out);
        if (out.ok) d["report"] = out.message;
        return d;
    }

    /// What one session wrote, in order — where a consolidation starts (overview §4.11). Each
    /// entry says whether it is still a head and what already generalises it, so a second
    /// consolidation in the same session does not state a pattern twice.
    py::dict review(std::uint64_t session) {
        {
            py::gil_scoped_release nogil;
            m_service->refresh();
        }
        const auto snap = m_service->current();
        py::list written;
        for (std::size_t i = 0; i < snap->size(); ++i) {
            const memory_id m(static_cast<std::uint32_t>(i));
            const auto& r = snap->record(m);
            if (r.session != session) continue;
            py::dict e;
            e["memory"] = ref(m);
            e["key"] = r.key.hex().substr(0, 12);
            e["title"] = r.title;
            e["origin"] = r.origin;
            e["head"] = snap->is_head(m);
            std::vector<std::string> tags;
            for (const auto t : snap->tags_of(m).members()) tags.push_back(snap->tax().info(tag_id(t)).qualified);
            e["tags"] = tags;
            e["generalises"] = refs_of(*snap, r.generalises);
            if (!r.generalises.empty()) e["accepted"] = snap->is_accepted(m);
            std::vector<std::string> gen_by;
            for (const auto x : snap->generalised_by(m)) gen_by.push_back(ref(x));
            e["generalised_by"] = gen_by;
            written.append(e);
        }
        py::dict d;
        d["ok"] = true;
        d["session"] = session;
        d["version"] = snap->version();
        d["written"] = written;
        return d;
    }

    /// One memory in full: its text, lineage, tags, and what has been observed of it.
    py::dict show(const std::string& memory) {
        const auto snap = m_service->current();
        const auto m = m_service->resolve(memory);
        if (!m) return refused({"unknown memory", memory + " names no memory — use #n from a read, or a key of at least 8 characters", {}});
        const auto& r = snap->record(*m);
        py::dict d;
        d["ok"] = true;
        d["memory"] = ref(*m);
        d["key"] = r.key.hex();
        d["slug"] = r.slug;
        d["title"] = r.title;
        d["text"] = m_service->text_of(*snap, *m).value_or("");
        d["origin"] = r.origin;
        d["created"] = r.created;
        d["author"] = r.author;
        d["reason"] = r.reason;
        d["head"] = snap->is_head(*m);
        std::vector<std::string> tags;
        for (const auto t : snap->tags_of(*m).members()) tags.push_back(snap->tax().info(tag_id(t)).qualified);
        d["tags"] = tags;
        std::vector<std::string> sup, by, seen;
        for (const auto& k : r.supersedes)
            if (auto x = snap->find(k)) sup.push_back(ref(*x));
        for (const auto x : snap->superseded_by(*m)) by.push_back(ref(x));
        for (const auto& k : r.seen)
            if (auto x = snap->find(k)) seen.push_back(ref(*x));
        d["supersedes"] = sup;
        d["superseded_by"] = by;
        d["generalises"] = refs_of(*snap, r.generalises);
        if (!r.generalises.empty()) d["accepted"] = snap->is_accepted(*m);
        std::vector<std::string> gen_by;
        for (const auto x : snap->generalised_by(*m)) gen_by.push_back(ref(x));
        d["generalised_by"] = gen_by;
        d["seen"] = seen;
        d["cites"] = r.cites;
        if (!r.corpus.empty()) d["corpus"] = r.corpus;
        const auto c = m_service->counts(r.key);
        d["counters"] = py::dict(py::arg("included") = c.included, py::arg("useful") = c.useful,
                                 py::arg("not_needed") = c.not_needed, py::arg("misleading") = c.misleading);
        return d;
    }

    py::list proposals() const {
        py::list out;
        const auto s = m_service->current();
        for (const auto& p : s->proposals) {
            py::dict d;
            d["concept"] = p.concept_name;
            d["dimension"] = p.dimension;
            d["entry"] = entry_dict(p.entry);
            std::vector<std::string> askers;
            for (const auto& k : p.asked_by)
                if (auto m = s->find(k)) askers.push_back(ref(*m));
            d["asked_by"] = askers;
            out.append(d);
        }
        return out;
    }

    /// Hand-written corpus blocks into the store (Feature 4).
    py::dict ingest(const std::string& path) {
        const std::filesystem::path p = path;
        const std::string text = F::read_file(p);
        auto parsed = F::parse_corpus(text, p.filename().string());
        ingest_outcome out;
        {
            py::gil_scoped_release nogil;
            out = m_service->ingest(parsed.blocks, p.filename().string(), digest::of(text).hex(), p.filename().string());
        }
        py::dict d;
        d["ok"] = parsed.errors.empty() && out.refused.empty();
        d["added"] = out.added;
        d["superseded"] = out.superseded;
        d["unchanged"] = out.unchanged;
        std::vector<std::string> refused = parsed.errors;
        refused.insert(refused.end(), out.refused.begin(), out.refused.end());
        d["refused"] = refused;
        return d;
    }

    /// Every vocabulary value that cites a source, with its full locator — what checking a drafted
    /// pack's citations against the documents needs (02 §5.3).
    py::list sources() const {
        const auto s = m_service->current();
        const auto& tax = s->tax();
        py::list out;
        for (std::size_t i = 0; i < tax.tags(); ++i) {
            const auto& ti = tax.info(tag_id(static_cast<tag_id::value_type>(i)));
            if (!ti.source) continue;
            py::dict d;
            d["tag"] = ti.qualified;
            d["doc"] = ti.source->doc;
            d["line"] = ti.source->line;
            d["lines"] = ti.source->lines ? ti.source->lines : 1;
            d["passage"] = ti.source->passage;
            out.append(d);
        }
        return out;
    }

    py::list receipts() const {
        py::list out;
        for (const auto& r : m_store->receipts()) out.append(receipt_dict(r));
        return out;
    }

private:
    py::dict receipt_dict(const receipt& r) const {
        py::dict d;
        d["snapshot"] = r.snapshot.hex();
        d["version"] = r.version;
        d["rules"] = r.rules;          // a rerun under other ranking rules is not the same run
        d["taxonomy"] = r.taxonomy.hex();
        d["hard"] = r.asked.hard;
        d["soft"] = r.asked.soft;
        d["max"] = r.asked.max_memories;
        d["budget"] = r.asked.budget;
        if (!r.asked.term.empty()) d["term"] = r.asked.term;
        std::vector<std::string> keys;
        for (const auto& k : r.keys) keys.push_back(k.hex());
        d["keys"] = keys;
        d["time"] = r.time;
        d["session"] = r.session;
        return d;
    }

    py::dict match_dict(const snapshot& s, const match& m, bool with_text) const {
        const auto& r = s.record(m.id);
        py::dict d;
        d["memory"] = ref(m.id);
        d["key"] = r.key.hex().substr(0, 12);
        d["title"] = r.title;
        if (with_text) d["text"] = m_service->text_of(s, m.id).value_or("");
        d["rank"] = m.rank;
        d["score"] = score_text(m.soft_score);
        d["hard_matched"] = names(s.tax(), m.hard_matched);
        d["soft_matched"] = names(s.tax(), m.soft_matched);
        d["soft_missed"] = names(s.tax(), m.soft_missed);
        d["tokens"] = m.tokens;
        if (!m.evidence.empty()) d["evidence"] = evidence_list(s, m.evidence);
        return d;
    }

    /// The instances folded under a generalisation: named, not placed — `show` one for its text.
    static py::list evidence_list(const snapshot& s, const std::vector<memory_id>& ids) {
        py::list out;
        for (const auto id : ids) {
            py::dict e;
            e["memory"] = ref(id);
            e["key"] = s.record(id).key.hex().substr(0, 12);
            e["title"] = s.record(id).title;
            e["tokens"] = s.record(id).tokens;
            out.append(e);
        }
        return out;
    }

    py::dict read_dict(const read_outcome& out, bool with_receipt = true) const {
        if (!out.ok) return refused(out.why);
        const snapshot& s = *out.snap;
        py::dict d;
        d["ok"] = true;
        d["snapshot"] = s.head().hex();
        d["version"] = s.version();
        d["corpus"] = out.ctx.corpus;
        d["candidates"] = out.ctx.candidates;
        if (out.ctx.term_matched) d["term_matched"] = *out.ctx.term_matched;
        d["folded"] = out.ctx.folded;
        d["tokens"] = out.ctx.tokens;
        d["over_budget"] = out.ctx.over_budget;
        if (out.ctx.procedure) {
            match pm;
            pm.id = *out.ctx.procedure;
            pm.tokens = s.record(pm.id).tokens;
            py::dict pd;
            pd["memory"] = ref(pm.id);
            pd["key"] = s.record(pm.id).key.hex().substr(0, 12);
            pd["title"] = s.record(pm.id).title;
            // Named, not paid for, when it alone is larger than the budget: the reader learns the
            // procedure exists without it swallowing everything that was asked for.
            pd["text"] = out.ctx.procedure_oversized ? std::string() : m_service->text_of(s, pm.id).value_or("");
            pd["tokens"] = pm.tokens;
            if (out.ctx.procedure_oversized) pd["over_budget"] = true;
            if (!out.ctx.procedure_evidence.empty()) pd["evidence"] = evidence_list(s, out.ctx.procedure_evidence);
            d["procedure"] = pd;
        } else {
            d["procedure"] = py::none();
        }
        if (!out.ctx.procedure_note.empty()) d["procedure_note"] = out.ctx.procedure_note;
        py::list mems;
        for (const auto& m : out.ctx.selected) mems.append(match_dict(s, m, true));
        d["memories"] = mems;
        d["skipped"] = out.ctx.skipped.size();  // ranked but not placed; `facets` says what they carry
        d["budget_dropped"] = out.ctx.budget_dropped;
        if (!out.ctx.standing.empty()) d["standing"] = evidence_list(s, out.ctx.standing);  // preferences not placed
        py::dict facets;
        for (const auto& [t, n] : out.ctx.facets) facets[py::str(s.tax().info(t).qualified)] = n;
        d["facets"] = facets;
        d["coverage"] = coverage_dict(out.ctx);
        if (with_receipt) d["receipt"] = receipt_dict(out.rec);
        return d;
    }

    /// Which documents the candidates rest on, and which inventoried documents none of them cites
    /// — so a read that found nothing relevant also says where the store has nothing (04 §3.8).
    py::dict coverage_dict(const context& ctx) const {
        py::dict d;
        py::dict cited;
        std::vector<std::string> not_cited;
        for (const auto& [doc, n] : ctx.by_source) cited[py::str(doc)] = n;
        for (const auto& id : m_store->inventory_ids())
            if (std::none_of(ctx.by_source.begin(), ctx.by_source.end(), [&](const auto& x) { return x.first == id; })) not_cited.push_back(id);
        d["cited"] = cited;
        d["uncited"] = ctx.uncited;
        // A store with fifty sources paid for all fifty names on every read that wanted none of them.
        const std::size_t shown = 8;
        if (not_cited.size() > shown) {
            d["not_cited_count"] = not_cited.size();
            not_cited.resize(shown);
        }
        d["not_cited"] = not_cited;
        return d;
    }

    /// A memory as a change left it: its tags and whether it is still a head, so the caller need
    /// not `show` it to confirm.
    static void stand(py::dict& d, const snapshot& s, memory_id m) {
        std::vector<std::string> tags;
        for (const auto t : s.tags_of(m).members()) tags.push_back(s.tax().info(tag_id(t)).qualified);
        d["memory"] = ref(m);
        d["tags"] = tags;
        d["head"] = s.is_head(m);
    }

    static py::dict write_dict(const write_outcome& out) {
        if (!out.ok) return refused(out.why);
        py::dict d;
        d["ok"] = true;
        if (out.snap) stand(d, *out.snap, out.id);
        d["memory"] = ref(out.id);
        d["key"] = out.key.hex();
        d["slug"] = out.slug;
        d["version"] = out.version;
        if (!out.verdict.empty()) d["verdict"] = out.verdict;
        return d;
    }

    static py::dict op_dict(const op_outcome& out) {
        if (!out.ok) return refused(out.why);
        py::dict d;
        d["ok"] = true;
        d["message"] = out.message;
        if (out.version) d["version"] = out.version;
        if (out.snap && out.id) stand(d, *out.snap, *out.id);
        return d;
    }

    std::unique_ptr<F::store> m_store;
    std::unique_ptr<service> m_service;
};

}  // namespace pygim::enact::adapter
