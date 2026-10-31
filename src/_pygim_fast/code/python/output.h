#pragma once
// code/python/output.h — what a parse of one file writes: the rows of its tree (rows.h) and its
// strings' literals, each a run of pieces; and the mark a checkpoint rewinds it to.
//
// CORE layer: pybind-free.

#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include "rows.h"

namespace pygim::code::python {

/// What a parse of one file writes: the rows of its tree, and its strings' literals, each a run of
/// pieces. A worker keeps one between files, so the rows' room is made once; a tree takes the
/// pieces and literals.
class parse_output {
public:
    /// How much is written: where a checkpoint rewinds to.
    class mark {
    public:
        mark(std::size_t rows, std::size_t pieces, std::size_t literals) noexcept
            : m_rows(static_cast<std::uint32_t>(rows)), m_pieces(static_cast<std::uint32_t>(pieces)), m_literals(static_cast<std::uint32_t>(literals)) {}

    private:
        friend class parse_output;
        std::uint32_t m_rows, m_pieces, m_literals;
    };

    [[nodiscard]] row_writer& rows() noexcept { return m_rows; }
    [[nodiscard]] const row_writer& rows() const noexcept { return m_rows; }

    /// A literal of `pieces`, in order: its index.
    std::uint32_t add_literal(std::span<const literal_piece> pieces) {
        const auto first = static_cast<std::uint32_t>(m_pieces.size());
        m_pieces.insert(m_pieces.end(), pieces.begin(), pieces.end());
        m_literals.emplace_back(first, static_cast<std::uint32_t>(pieces.size()));
        return static_cast<std::uint32_t>(m_literals.size() - 1);
    }

    [[nodiscard]] mark written() const noexcept { return {m_rows.size(), m_pieces.size(), m_literals.size()}; }
    void rewind(const mark& m) {
        m_rows.truncate(m.m_rows);
        m_pieces.resize(m.m_pieces);
        m_literals.resize(m.m_literals);
    }
    void clear() noexcept {
        m_rows.clear();
        m_pieces.clear();
        m_literals.clear();
    }
    /// row_writer::keep_room_for: the rows emptied, room for `rows` kept and the rest let go.
    void keep_room_for(std::size_t rows) { m_rows.keep_room_for(rows); }

    /// The pieces and the literals, moved out to a tree; the rows stay for the next file. Copied
    /// out at their size instead, so their room stayed too, Lib's parse on every core took 66-68 ms,
    /// not 60-62: the copies cost more than the regrowth they saved.
    [[nodiscard]] std::vector<literal_piece> take_pieces() noexcept { return std::move(m_pieces); }
    [[nodiscard]] std::vector<literal> take_literals() noexcept { return std::move(m_literals); }

private:
    row_writer m_rows;
    std::vector<literal_piece> m_pieces;
    std::vector<literal> m_literals;
};

}  // namespace pygim::code::python
