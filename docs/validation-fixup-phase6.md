# FIXUP Phase 6 — Bounded runtime log reads

Date: 2026-09-06. Base revision: `c0d9517`, plus the Phase 6 changes delivered
with this record. Implementation and automated checks are complete; the recorded
interactive manager acceptance gate remains open.

## Limits and presentation

`RuntimeLogReader` opens a fresh handle for each refresh and captures its length.
It reads only the last 1,048,576 bytes, once, with no follow-up scan or append
chasing. Short reads and early EOF do not trigger a restart. The stream overload
uses the same production algorithm and makes actual I/O observable in tests.

The default is 500 returned records; callers may request 1–10,000. Each record
retains at most 4,096 source bytes, keeping its recent suffix when oversized.
`[truncated] ` identifies a clipped record, and ` [partial record]` identifies an
unterminated final record. These annotations are additional to the source-byte
limit (at most 4,125 UTF-16 code units per returned string). Ordinary older records
outside the tail are omitted. The UI preserves the annotations as plain text.

One 1 MiB byte buffer and one reusable 4 KiB character buffer bound working
storage independently of file length. Total decoded source content is also
bounded by the byte window; line metadata and annotations are bounded by the
requested record limit. UTF-8 continuation bytes at a clipped start are skipped;
an incomplete trailing code point is withheld until the next refresh. Invalid
UTF-8 inside a record uses the standard replacement fallback.

Concurrent appends are excluded after the captured length. Concurrent clearing
may shorten a snapshot; arbitrary in-place rewriting is not an atomic snapshot.
A later refresh reopens the path, including after rotation/replacement. Missing
files return an empty result. Cancellation is checked before opening, during each
read, and during line extraction.

The manager retains its single active refresh guard. Hide and Clear invalidate
older refresh results; Clear also prevents a new refresh until it completes.
The existing optional visible-console timer and manual refresh remain available.

## Automated evidence

- Debug and Release manager core integration suites pass.
- Full managed solution Debug build with `RunAnalyzers=true` and
  `AnalysisLevel=latest-all` passes without warnings or errors.
- Fixtures cover a 3 MiB newline-free record, oversized JSON followed by a useful
  recent record, multibyte UTF-8 across both caps, an incomplete UTF-8 final record,
  many small lines, replacement, clearing, and pre-canceled reads.
- An 8 GiB virtual seekable file grows on every 8 KiB read. Its measured bytes read
  are exactly 1 MiB, with the requested seven-line bound and per-line bounds.
- The same fixture clears after its first read or cancels from that read, proving
  early EOF and mid-read cancellation stop further I/O.

## Remaining acceptance

Run the built manager against isolated oversized log fixtures and record opening,
manual/live refreshing, hiding/reopening, and confirmed clearing. Verify visible
truncation feedback and responsiveness. No interactive manager walkthrough is
claimed by the core tests or successful WPF build; Phase 6 remains in progress
until this gate is recorded.
