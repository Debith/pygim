"""Tests for pygim.rng — correctness, determinism, and statistical sanity.

The generator's contract: the emitted sequence is a pure function of the
seed — identical across the SIMD and scalar paths, any thread count, and any
way the total is split across calls. Correctness is anchored to an
independent pure-Python oracle implemented here from the published algorithm
specs, which is itself validated against the rand_xoshiro known-answer
vectors before it is trusted to judge the C++ implementation.
"""

import pytest

np = pytest.importorskip("numpy", reason="pygim.rng requires numpy at runtime")
pytest.importorskip("pygim.rng", reason="C++ rng extension not built")
from pygim.rng import Rng

_M = (1 << 64) - 1
_BLOCK = 262144  # elements per block (kBlockElems in core.h)
_LANES = 16


# ---------------------------------------------------------------------------
# Pure-Python oracle (independent reimplementation of the published specs)
# ---------------------------------------------------------------------------


def _sm_mix(z):
    z = ((z ^ (z >> 30)) * 0xBF58476D1CE4E5B9) & _M
    z = ((z ^ (z >> 27)) * 0x94D049BB133111EB) & _M
    return z ^ (z >> 31)


def _sm_at(x0, n):
    return _sm_mix((x0 + (n + 1) * 0x9E3779B97F4A7C15) & _M)


def _rotl(x, k):
    return ((x << k) | (x >> (64 - k))) & _M


class _Xoshiro:
    def __init__(self, state):
        self.s = list(state)

    def next(self):
        s = self.s
        result = (_rotl((s[0] + s[3]) & _M, 23) + s[0]) & _M
        t = (s[1] << 17) & _M
        s[2] ^= s[0]
        s[3] ^= s[1]
        s[1] ^= s[2]
        s[0] ^= s[3]
        s[2] ^= t
        s[3] = _rotl(s[3], 45)
        return result


def _derive_word(seed, idx):
    """State-word derivation: mix(seed + mix(idx * golden)).

    The counter goes through its own mix round before combining with the
    seed, so related seeds cannot alias whole streams (see core.h).
    """
    return _sm_mix((seed + _sm_mix((idx * 0x9E3779B97F4A7C15) & _M)) & _M)


def _oracle_stream(seed, stream_index):
    return _Xoshiro([_derive_word(seed, stream_index * 4 + k + 1) for k in range(4)])


def _oracle_values(seed, block, count):
    """First `count` outputs of block `block` in emission order."""
    streams = [_oracle_stream(seed, block * _LANES + lane) for lane in range(_LANES)]
    return [streams[i % _LANES].next() for i in range(count)]


# ---------------------------------------------------------------------------
# Oracle self-validation against published known-answer vectors
# ---------------------------------------------------------------------------


def test_oracle_xoshiro_reference_vector():
    """The Python oracle reproduces the rand_xoshiro xoshiro256++ vector.

    The oracle is only a trustworthy judge of the C++ implementation if it
    itself matches the reference implementation. Vector source:
    rust-random/rngs rand_xoshiro, produced with the canonical C code at
    prng.di.unimi.it, seed state {1, 2, 3, 4}.
    """
    g = _Xoshiro([1, 2, 3, 4])
    expected = [
        41943041,
        58720359,
        3588806011781223,
        3591011842654386,
        9228616714210784205,
        9973669472204895162,
        14011001112246962877,
        12406186145184390807,
        15849039046786891736,
        10450023813501588000,
    ]
    assert [g.next() for _ in range(10)] == expected


def test_oracle_splitmix_reference_vector():
    """The Python oracle reproduces the rand_xoshiro SplitMix64 vector.

    Validates the counter-based random-access form used for O(1) stream
    derivation: output n of the stream seeded with x0 must equal
    mix(x0 + (n+1)*golden). Seed 1477776061723855037 from the rand_xoshiro
    test suite.
    """
    seed = 1477776061723855037
    expected = [
        1985237415132408290,
        2979275885539914483,
        13511426838097143398,
        8488337342461049707,
        15141737807933549159,
    ]
    assert [_sm_at(seed, n) for n in range(5)] == expected


# ---------------------------------------------------------------------------
# C++ implementation vs oracle
# ---------------------------------------------------------------------------


def test_uint64_matches_oracle():
    """The C++ engine emits exactly the oracle's interleaved-stream sequence.

    Cross-language known-answer test: 256 values covering 16 rows of all 16
    streams. A transcription error in any constant, the stream derivation,
    or the interleaving order fails here.
    """
    seed = 42
    got = Rng(seed).uint64(256)
    assert list(got) == _oracle_values(seed, block=0, count=256)


def test_block_boundary_switches_streams():
    """Crossing a block boundary switches to freshly derived streams.

    Block b uses streams [16b, 16b+16); the first values after the boundary
    must match the oracle's block-1 streams from their initial states. This
    pins the block/stream indexing that thread-parallel fills rely on.
    """
    seed = 9001
    n = _BLOCK + 64
    got = Rng(seed, threads=1).uint64(n)
    assert list(got[:64]) == _oracle_values(seed, block=0, count=64)
    assert list(got[_BLOCK:]) == _oracle_values(seed, block=1, count=64)


def test_random_uses_numpy_53bit_mapping():
    """random() equals (uint64 >> 11) * 2**-53 draw for draw.

    The float64 mapping is numpy's exact formula, so distribution granularity
    matches numpy. Verified by cross-checking the two public APIs against
    each other from the same seed.
    """
    u = Rng(7).uint64(4096)
    d = Rng(7).random(4096)
    assert np.array_equal(d, (u >> np.uint64(11)).astype(np.float64) * 2.0**-53)


# ---------------------------------------------------------------------------
# Determinism contracts
# ---------------------------------------------------------------------------


def test_seed_reproducibility_and_distinct_seeds():
    """Same seed reproduces the sequence; different seeds do not.

    The basic RNG contract, plus a guard against the seed being ignored
    (e.g. a hard-coded base slipping into stream derivation).
    """
    a = Rng(123).random(10_000)
    b = Rng(123).random(10_000)
    c = Rng(124).random(10_000)
    assert np.array_equal(a, b)
    assert not np.array_equal(a, c)


def test_simd_and_scalar_paths_identical():
    """The scalar path replays the SIMD lane layout bit-for-bit.

    Output must be a function of the seed, not the hardware: the AVX2
    conversion sequence is exact for v < 2^53, so both paths compute the
    same doubles. Guards against lane-order and conversion drift.
    """
    if Rng(0).simd != "avx2":
        pytest.skip("AVX2 unavailable; only one path to compare")
    fast = Rng(31, simd=True, threads=1).random(500_000)
    slow = Rng(31, simd=False, threads=1).random(500_000)
    assert np.array_equal(fast, slow)


def test_thread_count_invariance():
    """Any thread count produces the identical sequence.

    Blocks own disjoint stream ranges derived O(1) from the seed, so the
    partitioning of blocks across threads cannot influence values. Uses a
    size spanning many blocks including a partial tail.
    """
    n = 5 * _BLOCK + 12345
    single = Rng(55, threads=1).random(n)
    many = Rng(55, threads=8).random(n)
    assert np.array_equal(single, many)


def test_call_split_invariance():
    """Splitting one large request into many calls yields the same stream.

    The resume cache carries live stream states across calls, so
    random(a) + random(b) == random(a + b) elementwise — including splits
    that end mid-row and mid-block.
    """
    n = _BLOCK + 4242
    whole = Rng(2024).random(n)
    r = Rng(2024)
    chunks = [1, 15, 16, 17, 1000, _BLOCK - 1000, 0]
    chunks.append(n - sum(chunks))  # remainder ends mid-row, mid-block
    parts = [r.random(k) for k in chunks]
    assert sum(p.size for p in parts) == n
    assert np.array_equal(np.concatenate(parts), whole)


# ---------------------------------------------------------------------------
# API surface
# ---------------------------------------------------------------------------


def test_fill_in_place_any_shape():
    """fill() writes into caller-owned arrays of any C-contiguous shape.

    In-place filling is the zero-allocation fast path; shape must not
    matter as long as the buffer is contiguous.
    """
    out = np.zeros((250, 40))
    Rng(1).fill(out)
    assert out.min() >= 0.0 and out.max() < 1.0
    assert np.count_nonzero(out) > 9900  # zeros are ~2^-53-improbable

    flat = np.empty(10_000)
    Rng(1).fill(flat)
    assert np.array_equal(out.ravel(), flat)


def test_fill_rejects_wrong_dtype_and_noncontiguous():
    """fill() must reject buffers it cannot fill in place.

    A float32 or non-contiguous array would require conversion — which
    pybind11 does by copying, silently discarding the fill. The binding
    forbids the conversion instead of losing writes.
    """
    with pytest.raises(TypeError):
        Rng(1).fill(np.empty(8, dtype=np.float32))
    with pytest.raises(TypeError):
        Rng(1).fill(np.empty((8, 8))[:, ::2])
    with pytest.raises(TypeError):
        Rng(1).fill_uint64(np.empty(8, dtype=np.int64))
    with pytest.raises(TypeError):
        Rng(1).fill([0.0] * 8)  # a list would be converted, losing the fill
    frozen = np.empty(8)
    frozen.setflags(write=False)
    with pytest.raises(ValueError):
        Rng(1).fill(frozen)
    with pytest.raises(TypeError):
        Rng(1).fill(np.empty(8, dtype=">f8"))  # byte order mismatch
    # element-misaligned buffer: contiguous and writable, but UB to write
    misaligned = np.frombuffer(bytearray(65), dtype=np.float64, offset=1)
    with pytest.raises(TypeError):
        Rng(1).fill(misaligned)


def test_fill_accepts_equivalent_dtype_spellings():
    """dtype equivalence, not dtype-num identity, decides acceptance.

    On LP64 platforms np.dtype('Q') and np.dtype(np.uint64) compare equal
    but carry different dtype nums; both spellings must be fillable.
    """
    for spelling in ("Q", "L", np.uint64):
        buf = np.zeros(64, dtype=spelling)
        if buf.dtype != np.dtype(np.uint64):
            continue  # 'L' is not 64-bit on this platform
        Rng(1).fill_uint64(buf)
        assert np.count_nonzero(buf) > 0


def test_size_and_seed_validation():
    """Hostile inputs fail loudly with the right exception types.

    Type errors (str, Decimal) are TypeError; range errors (-1, 2**64) are
    ValueError. Integer-likes with __index__ (numpy ints) are accepted;
    Decimal must not be silently truncated.
    """
    from decimal import Decimal

    with pytest.raises(ValueError):
        Rng(1).random(-1)
    assert Rng(1).random(0).size == 0
    with pytest.raises(ValueError):
        Rng(-1)
    with pytest.raises(ValueError):
        Rng(1 << 64)
    with pytest.raises(TypeError):
        Rng("not a seed")
    with pytest.raises(TypeError):
        Rng(Decimal("3.9"))
    with pytest.raises(TypeError):
        Rng(3.0)
    assert Rng(np.uint64(5)).seed == 5
    with pytest.raises(ValueError):
        Rng(1, threads=-1)


def test_properties_and_repr():
    """seed/simd/threads introspection matches construction arguments."""
    r = Rng(99, threads=4, simd=False)
    assert r.seed == 99
    assert r.simd == "scalar"
    assert r.threads == 4
    assert "Rng(seed=99" in repr(r)
    assert Rng(0).simd in ("avx2", "scalar")


def test_seed_none_gives_fresh_entropy():
    """seed=None draws real entropy: two generators must not collide."""
    assert not np.array_equal(Rng().random(64), Rng().random(64))


def test_nt_store_path_bit_exact():
    """The non-temporal store path emits the identical sequence.

    NT kernels engage only for >= 32 MiB of full blocks on a 32-byte-aligned
    destination — far above the other exact tests. random() allocates
    64-byte-aligned output, so this size deterministically exercises the NT
    kernels; a lane permutation or offset bug confined to that path fails
    here and nowhere else.
    """
    n = 40 * _BLOCK  # ~84 MB of float64, well past kNtThresholdBytes
    a = Rng(5, threads=1).random(n)
    assert a.ctypes.data % 64 == 0  # aligned allocation, NT-eligible
    assert np.array_equal(a, Rng(5, threads=1, simd=False).random(n))
    assert np.array_equal(a, Rng(5, threads=8).random(n))

    u = Rng(5, threads=1).uint64(n)
    assert np.array_equal(u, Rng(5, threads=1, simd=False).uint64(n))

    # NT region starting after a mid-block resume must continue seamlessly.
    r = Rng(5, threads=1)
    head = r.random(777)
    tail = r.random(n - 777)
    assert np.array_equal(np.concatenate([head, tail]), a)


def test_related_seeds_are_independent():
    """Seeds at small golden-ratio lattice distances share no streams.

    The naive additive derivation had an exact defect: Rng(s + 4*golden)
    re-emitted Rng(s)'s streams shifted by one lane. The mixed-counter
    derivation breaks that lattice; draws from related seeds must be
    disjoint (any overlap in 64-bit space is a ~2**-47 accident).
    """
    golden = 0x9E3779B97F4A7C15
    base = 123456789
    u0 = Rng(base).uint64(100_000)
    for delta in (golden, 4 * golden, 64 * golden):
        other = Rng((base + delta) % (1 << 64)).uint64(100_000)
        assert np.intersect1d(u0, other).size == 0


# ---------------------------------------------------------------------------
# Statistical sanity (10M samples; bounds are ~5 sigma, so false-failure
# probability is ~1e-6 per assertion)
# ---------------------------------------------------------------------------


def test_uniform_statistics():
    """Mean, variance, range, histogram, and lag-1 correlation of random().

    Not a substitute for BigCrush (xoshiro256++ passes that upstream), but
    catches gross implementation faults: biased conversion, stuck lanes,
    range escapes, or correlated interleaving.
    """
    n = 10_000_000
    x = Rng(31415).random(n)

    assert x.min() >= 0.0
    assert x.max() < 1.0

    sigma_mean = (1.0 / np.sqrt(12.0)) / np.sqrt(n)
    assert abs(x.mean() - 0.5) < 5 * sigma_mean

    assert abs(x.var() - 1.0 / 12.0) < 1e-4

    counts, _ = np.histogram(x, bins=100, range=(0.0, 1.0))
    expected = n / 100
    chi2 = ((counts - expected) ** 2 / expected).sum()
    assert chi2 < 99 + 5 * np.sqrt(2 * 99)  # df=99, ~5 sigma

    lag1 = np.corrcoef(x[:-1], x[1:])[0, 1]
    assert abs(lag1) < 5 / np.sqrt(n)

    # lag-16 is each stream's own serial correlation under the interleave
    lag16 = np.corrcoef(x[:-16], x[16:])[0, 1]
    assert abs(lag16) < 5 / np.sqrt(n)

    # cross-lane correlations catch related-seeding between the 16 streams
    lanes = x[: (n // 16) * 16].reshape(-1, 16)
    corr = np.corrcoef(lanes, rowvar=False)
    off_diag = corr[~np.eye(16, dtype=bool)]
    assert np.abs(off_diag).max() < 5 / np.sqrt(n / 16)


def test_uint64_bit_statistics():
    """Raw draws look like independent uniform 64-bit words.

    Popcount mean must be ~32 and 10M draws must all be distinct (birthday
    collision probability ~3e-6 for a healthy 64-bit generator; duplicates
    would indicate stream overlap or state reuse).
    """
    n = 10_000_000
    u = Rng(2718).uint64(n)

    pc = np.unpackbits(u.view(np.uint8)).sum() / n
    assert abs(pc - 32.0) < 0.02

    # per-bit-position bias: each of the 64 positions must be ~n/2 ones
    bits = np.unpackbits(u.view(np.uint8)).reshape(n, 64)
    ones = bits.sum(axis=0, dtype=np.int64)
    z = np.abs(ones - n / 2) / np.sqrt(n / 4)
    assert z.max() < 5.5  # 64 positions; Bonferroni-ish headroom over 5 sigma

    assert np.unique(u).size == n


if __name__ == "__main__":
    from pygim.core.testing import run_tests

    run_tests(__file__, pytest_args=["-v", "--tb=short"])
