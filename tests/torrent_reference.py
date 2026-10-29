"""Independent BEP 3 / BEP 52 oracle using only Python's standard library.
Run directly with the torrent_tests executable; also registered in CTest.
"""
import hashlib
from pathlib import Path
import subprocess
import sys
import tempfile
from urllib.parse import parse_qs, urlsplit


def decode(data):
    def take(pos):
        first = data[pos:pos + 1]
        if first == b'i':
            end = data.index(b'e', pos)
            value = int(data[pos + 1:end])
            assert data[pos + 1:end] == str(value).encode()
            return value, end + 1
        if first in (b'd', b'l'):
            result = {} if first == b'd' else []
            pos += 1
            previous = None
            while data[pos:pos + 1] != b'e':
                key, pos = take(pos)
                if isinstance(result, dict):
                    assert isinstance(key, bytes)
                    assert previous is None or previous < key
                    previous = key
                    value, pos = take(pos)
                    result[key] = value
                else:
                    result.append(key)
            return result, pos + 1
        end = data.index(b':', pos)
        size = int(data[pos:end])
        assert data[pos:end] == str(size).encode()
        end += 1
        return data[end:end + size], end + size
    result, end = take(0)
    assert end == len(data)
    return result


def encode(value):
    if isinstance(value, bytes):
        return str(len(value)).encode() + b':' + value
    if isinstance(value, int):
        return b'i' + str(value).encode() + b'e'
    if isinstance(value, list):
        return b'l' + b''.join(map(encode, value)) + b'e'
    return b'd' + b''.join(encode(k) + encode(value[k]) for k in sorted(value)) + b'e'


def sha(data):
    return hashlib.sha256(data).digest()


def merkle(nodes, width=None, zero=bytes(32)):
    if not nodes:
        return b''
    width = width or 1 << (len(nodes) - 1).bit_length()
    nodes = nodes + [zero] * (width - len(nodes))
    while len(nodes) > 1:
        nodes = [sha(nodes[i] + nodes[i + 1]) for i in range(0, len(nodes), 2)]
    return nodes[0]


def hashes(data, piece):
    # Build the entire leaf tree independently, then extract its piece layer.
    nodes = [sha(data[i:i + 16384]) for i in range(0, len(data), 16384)]
    if not nodes:
        return b'', b''
    root = merkle(nodes)
    layer = b''
    if len(data) > piece:
        width = piece // 16384
        layer = b''.join(merkle(nodes[i:i + width], width) for i in range(0, len(nodes), width))
    return root, layer


def walk(tree, prefix=()):
    for part, node in tree.items():
        if b'' in node:
            assert len(node) == 1
            yield prefix + (part,), node[b'']
        else:
            yield from walk(node, prefix + (part,))


def verify(path):
    mode, piece, single = path.with_suffix('.txt').read_text().splitlines()
    piece, single = int(piece), bool(int(single))
    sizes = list(map(int, path.with_suffix('.sizes').read_text().splitlines()))
    payloads = [bytes([ord('a') + i]) * size for i, size in enumerate(sizes)]
    paths = [(b'sample.bin',)] if single else [(f'dir{i}'.encode(), 'file-è.bin'.encode()) for i in range(len(sizes))]
    raw = path.read_bytes()
    torrent = decode(raw)
    assert encode(torrent) == raw
    info = torrent[b'info']
    encoded = encode(info)
    sha1, sha256, magnet = path.with_suffix('.hashes').read_text().splitlines()
    assert sha1 == (hashlib.sha1(encoded).hexdigest() if mode != 'v2' else '')
    assert sha256 == (hashlib.sha256(encoded).hexdigest() if mode != 'v1' else '')
    xt = parse_qs(urlsplit(magnet).query)['xt']
    assert xt == ([f'urn:btih:{sha1}'] if sha1 else []) + ([f'urn:btmh:1220{sha256}'] if sha256 else [])
    assert info[b'piece length'] == piece
    assert info[b'private'] == 1 and info[b'source'] == b'test'
    if mode != 'v1':
        assert info[b'meta version'] == 2
        entries = list(walk(info[b'file tree']))
        assert [p for p, _ in entries] == paths
        layers = {}
        for (_, props), data in zip(entries, payloads):
            root, layer = hashes(data, piece)
            assert props[b'length'] == len(data)
            assert props.get(b'pieces root', b'') == root
            assert bool(root) == (b'pieces root' in props)
            if layer:
                layers[root] = layer
        assert torrent[b'piece layers'] == layers
    else:
        assert b'meta version' not in info and b'file tree' not in info and b'piece layers' not in torrent
    if mode != 'v2':
        stream = bytearray()
        if single:
            assert info[b'length'] == sizes[0] and b'files' not in info
            stream.extend(payloads[0])
        else:
            assert b'length' not in info
            expected = []
            for i, (p, data) in enumerate(zip(paths, payloads)):
                expected.append({b'path': list(p), b'length': len(data)})
                stream.extend(data)
                if mode == 'hybrid' and i + 1 < len(paths) and len(data) % piece:
                    pad = (-len(data)) % piece
                    expected.append({b'attr': b'p', b'path': [b'.pad', f'{i}_{pad}'.encode()], b'length': pad})
                    stream.extend(bytes(pad))
            assert info[b'files'] == expected
        assert info[b'pieces'] == b''.join(hashlib.sha1(stream[i:i + piece]).digest() for i in range(0, len(stream), piece))
    else:
        assert not ({b'pieces', b'files', b'length'} & info.keys())


if __name__ == '__main__':
    with tempfile.TemporaryDirectory(prefix='releaseforge-torrents-') as tmp:
        subprocess.run([sys.argv[1], tmp], check=True)
        files = list(Path(tmp).glob('*.torrent'))
        assert len(files) == 162
        accepted = 0
        for path in files:
            verify(path)
            if '--libtorrent' in sys.argv[2:]:
                import libtorrent as lt
                sizes = list(map(int, path.with_suffix('.sizes').read_text().splitlines()))
                if sum(sizes) == 0:
                    # The app rejects empty sources before hashing. libtorrent does too.
                    try:
                        lt.torrent_info(path.read_bytes())
                    except RuntimeError:
                        continue
                    raise AssertionError('libtorrent unexpectedly accepted an empty torrent')
                info = lt.torrent_info(path.read_bytes())
                expected = path.with_suffix('.hashes').read_text().splitlines()
                h = info.info_hashes()
                assert (str(h.v1) if h.has_v1() else '') == expected[0]
                assert (str(h.v2) if h.has_v2() else '') == expected[1]
                accepted += 1
        if '--libtorrent' in sys.argv[2:]:
            print(f'libtorrent {lt.__version__}: {accepted} nonempty torrents accepted, infohashes match')
        print(f'Independent hashlib/bencode reference: {len(files)} torrents verified')
