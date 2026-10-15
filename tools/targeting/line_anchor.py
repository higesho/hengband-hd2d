"""Locate original source lines by hash, so patch scripts need not quote the original.

An anchor is the first 12 hex digits of the SHA-256 of a line with its surrounding
whitespace removed (latin1 bytes).  A block anchor is a list of such hashes that must
match consecutive lines.  Every anchor must match exactly once, otherwise the original
changed and the patch must be reviewed.  Only our own inserted text appears in the scripts.

Text is handled with "\n" line ends (callers normalize CRLF themselves).
"""
import hashlib


def line_hash(line):
    return hashlib.sha256(line.strip().encode('latin1')).hexdigest()[:12]


def _locate(lines, anchors, what):
    anchors = [anchors] if isinstance(anchors, str) else list(anchors)
    hashes = [line_hash(l) for l in lines]
    n = len(anchors)
    hits = [i for i in range(len(lines) - n + 1) if hashes[i:i + n] == anchors]
    if len(hits) != 1:
        raise ValueError(f'{what}: expected one anchored site, found {len(hits)}')
    return hits[0], n


def count(text, anchors):
    lines = text.split('\n')
    anchors = [anchors] if isinstance(anchors, str) else list(anchors)
    hashes = [line_hash(l) for l in lines]
    return sum(hashes[i:i + len(anchors)] == anchors for i in range(len(lines) - len(anchors) + 1))


def insert_after(text, anchors, new, what, skip=0, expect_brace=False):
    """Insert `new` (without trailing newline) after the anchored block (and `skip` following lines)."""
    lines = text.split('\n')
    i, n = _locate(lines, anchors, what)
    at = i + n + skip
    if expect_brace and lines[at - 1].strip() != '{':
        raise ValueError(what + ': the anchored line is not followed by an opening brace')
    lines[at:at] = new.split('\n')
    return '\n'.join(lines)


def insert_before(text, anchors, new, what):
    lines = text.split('\n')
    i, _ = _locate(lines, anchors, what)
    lines[i:i] = new.split('\n')
    return '\n'.join(lines)


def delete(text, anchors, what):
    lines = text.split('\n')
    i, n = _locate(lines, anchors, what)
    del lines[i:i + n]
    return '\n'.join(lines)


def replace(text, anchors, new, what):
    lines = text.split('\n')
    i, n = _locate(lines, anchors, what)
    lines[i:i + n] = new.split('\n')
    return '\n'.join(lines)


def line_end_offset(text, anchors, what):
    """Offset just after the newline that ends the anchored block."""
    lines = text.split('\n')
    i, n = _locate(lines, anchors, what)
    return sum(len(l) + 1 for l in lines[:i + n])


def line_start_offset(text, anchors, what):
    lines = text.split('\n')
    i, _ = _locate(lines, anchors, what)
    return sum(len(l) + 1 for l in lines[:i])


def edit_all(text, anchors, make):
    """Replace every anchored block (any number of hits, including none).

    `make` receives the original lines of the block and returns the new lines, so the
    replacement can reuse the original text without quoting it.  Returns (text, hits).
    """
    lines = text.split('\n')
    anchors = [anchors] if isinstance(anchors, str) else list(anchors)
    n = len(anchors)
    hashes = [line_hash(l) for l in lines]
    out, i, hits = [], 0, 0
    while i < len(lines):
        if hashes[i:i + n] == anchors:
            out.extend(make(lines[i:i + n]))
            i += n
            hits += 1
        else:
            out.append(lines[i])
            i += 1
    return '\n'.join(out), hits


def edit_exactly(text, anchors, make, count, what):
    """edit_all, but the number of hits must be exactly `count`."""
    result, hits = edit_all(text, anchors, make)
    if hits != count:
        raise ValueError(f'{what}: expected {count} anchored sites, found {hits}')
    return result


def original_line(text, anchor_hash):
    """The stripped text of the first line whose hash matches, so an inserted line that is
    identical to an original line can be taken from the file instead of being quoted."""
    for line in text.split('\n'):
        if line_hash(line) == anchor_hash:
            return line.strip()
    raise ValueError('original line not found: ' + anchor_hash)


def append_to_line(text, anchor_hash, suffix, what):
    """Append `suffix` to the end of the one anchored line (before a trailing CR)."""
    def make(lines):
        line = lines[0]
        cr = '\r' if line.endswith('\r') else ''
        return [line[:len(line) - len(cr)] + suffix + cr]
    return edit_exactly(text, anchor_hash, make, 1, what)


def prepend_to_line(text, anchor_hash, prefix, what):
    """Put `prefix` between the indentation and the text of the one anchored line."""
    def make(lines):
        line = lines[0]
        body = line.lstrip(' \t')
        return [line[:len(line) - len(body)] + prefix + body]
    return edit_exactly(text, anchor_hash, make, 1, what)
