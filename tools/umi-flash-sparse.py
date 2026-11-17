#!/usr/bin/env python3
"""Flash a large raw image to a partition in Android-sparse pieces, one piece on disk at a time.

  tools/umi-flash-sparse.py IMAGE PARTITION [PIECE_MB]        (phone in fastboot; PIECE_MB default 600)

Why: Homebrew fastboot 37 cannot read a 32 GiB raw image ("Failed reading from userdata"), and there is no
img2simg here. Each piece is an Android sparse file (format 1.0, 4 KiB blocks) covering the whole image
length: DONT_CARE before and after its own range, and inside it every block as RAW data or a FILL of zeros
(holes in the image file and all-zero blocks), so nothing the partition held before survives inside the image's
range. Each piece is flashed with `fastboot flash PARTITION piece`, checked for OKAY and deleted.
A log line per piece; --start N resumes from piece N (they are deterministic).
"""
import os, struct, subprocess, sys, tempfile

BLK = 4096
MAGIC, RAW, FILL, DONT_CARE = 0xED26FF3A, 0xCAC1, 0xCAC2, 0xCAC3


def chunk_hdr(t, blocks, data_len):
    return struct.pack('<HHII', t, 0, blocks, 12 + data_len)


def write_piece(path, total_blks, start, chunks, end):
    """chunks: list of (type, blocks, payload) covering [start, end)"""
    out = []
    if start:
        out.append((DONT_CARE, start, b''))
    out.extend(chunks)
    if end < total_blks:
        out.append((DONT_CARE, total_blks - end, b''))
    with open(path, 'wb') as f:
        f.write(struct.pack('<IHHHHIIII', MAGIC, 1, 0, 28, 12, BLK, total_blks, len(out), 0))
        for t, n, payload in out:
            f.write(chunk_hdr(t, n, len(payload)))
            f.write(payload)


def data_ranges(fd, size):
    """(offset, length) of the data (non-hole) regions, via SEEK_DATA / SEEK_HOLE"""
    off = 0
    while off < size:
        try:
            d = os.lseek(fd, off, os.SEEK_DATA)
        except OSError:
            return
        h = os.lseek(fd, d, os.SEEK_HOLE)
        yield d, min(h, size) - d
        off = h


def verify(path, image):
    """decode a piece and compare every RAW / FILL block with the image"""
    with open(path, 'rb') as f, open(image, 'rb') as img:
        magic, _, _, _, _, bs, tb, nc, _ = struct.unpack('<IHHHHIIII', f.read(28))
        assert magic == MAGIC and bs == BLK
        blk = 0
        for _ in range(nc):
            t, _, n, tot = struct.unpack('<HHII', f.read(12))
            data = f.read(tot - 12)
            if t in (RAW, FILL):
                img.seek(blk * BLK)
                want = img.read(n * BLK)
                got = data if t == RAW else data[:4] * (n * BLK // 4)
                if want != got:
                    sys.exit('MISMATCH in %s at block %d' % (path, blk))
            blk += n
        assert blk == tb, (blk, tb)
    print('  verified %s' % os.path.basename(path), flush=True)


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    if len(args) < 2:
        sys.exit(__doc__)
    image, part = args[0], args[1]
    piece_bytes = int(args[2]) * 1024 * 1024 if len(args) > 2 else 600 * 1024 * 1024
    first, dry = 0, 0
    for a in sys.argv[1:]:
        if a.startswith('--start='):
            first = int(a.split('=', 1)[1])
        if a.startswith('--dry='):
            dry = int(a.split('=', 1)[1])     # build, decode and compare the first N pieces; flash nothing
    size = os.path.getsize(image)
    if size % BLK:
        sys.exit('image size is not a multiple of 4096')
    total = size // BLK
    fd = os.open(image, os.O_RDONLY)
    zero = bytes(BLK)
    tmpdir = tempfile.mkdtemp(prefix='sparse-', dir=os.path.dirname(os.path.abspath(image)))

    # Walk the image as runs of blocks: [blk, kind, payload]; holes are zero runs.
    def runs():
        pos = 0
        for d, ln in data_ranges(fd, size):
            if d > pos:
                yield (pos // BLK, (d - pos) // BLK, None)
            os.lseek(fd, d, os.SEEK_SET)
            left = ln
            b = d // BLK
            while left > 0:
                buf = os.read(fd, min(left, 8 * 1024 * 1024))
                if not buf:
                    break
                for i in range(0, len(buf), BLK):
                    blk = buf[i:i + BLK]
                    yield (b, 1, None if blk == zero else blk)
                    b += 1
                left -= len(buf)
            pos = d + ln
        if pos < size:
            yield (pos // BLK, (size - pos) // BLK, None)

    piece, chunks, start, raw_bytes, cur = 0, [], 0, 0, 0

    def flush(end):
        nonlocal piece, chunks, start, raw_bytes
        if not chunks:
            return
        if piece >= first:
            path = os.path.join(tmpdir, 'piece-%03d.simg' % piece)
            write_piece(path, total, start, chunks, end)
            print('piece %d: blocks %d-%d (%.1f%% of the image), %d MiB of data' %
                  (piece, start, end, 100.0 * end / total, raw_bytes >> 20), flush=True)
            if dry:
                verify(path, image)
                os.unlink(path)
                piece += 1
                chunks, start, raw_bytes = [], end, 0
                if piece >= dry:
                    sys.exit('dry run: %d pieces decoded and identical to the image' % piece)
                return
            r = subprocess.run(['fastboot', 'flash', part, path], capture_output=True, text=True)
            text = r.stdout + r.stderr
            os.unlink(path)
            if r.returncode != 0 or 'Finished' not in text or 'FAILED' in text:
                print(text, flush=True)
                sys.exit('piece %d failed: rerun with --start=%d' % (piece, piece))
            print('piece %d: OKAY' % piece, flush=True)
        piece += 1
        chunks, start, raw_bytes = [], end, 0

    for b, n, payload in runs():
        if payload is None:
            if chunks and chunks[-1][0] == FILL:
                t, m, p = chunks[-1]
                chunks[-1] = (t, m + n, p)
            else:
                chunks.append((FILL, n, struct.pack('<I', 0)))
        else:
            if chunks and chunks[-1][0] == RAW:
                t, m, p = chunks[-1]
                p.append(payload)
                chunks[-1] = (t, m + 1, p)
            else:
                chunks.append((RAW, 1, [payload]))
            raw_bytes += BLK
        cur = b + n
        if raw_bytes >= piece_bytes:
            chunks = [(t, m, b''.join(p) if t == RAW else p) for t, m, p in chunks]
            flush(cur)
    chunks = [(t, m, b''.join(p) if t == RAW else p) for t, m, p in chunks]
    flush(total)
    os.rmdir(tmpdir)
    print('done: %d pieces, %s flashed with the whole image' % (piece, part), flush=True)


if __name__ == '__main__':
    main()
