"""Reads and writes the Harmonix archive formats used by Rock Band: The Beatles (Xbox 360).

- Rand2 stream cipher used by `main_xbox.hdr` and every `.dtb` (seed = first 4 bytes, LE).
- Archive header version 5 (`gen/main_xbox.hdr`): ark part sizes/names, a string blob and
  24-byte file entries whose offsets are global across all parts.
- Compiled DTA scripts (`.dtb`), old layout: arrays are `u16 count, u32 line` + nodes, each
  node a `u32 type` + payload. `load_dtb`/`dump_dtb` round-trip byte for byte.
- A small DTA text parser (`parse_dta`) for authoring new script snippets, and a printer
  (`format_dta`) for reading decompiled scripts.

Library only: used by `scripts/build_game_overlay.py` and `scripts/dtb2dta.py`.
"""
import re
import struct

# ---------------------------------------------------------------- cipher


def _rand2_stream(seed):
    s = seed if seed > 0 else (1 if seed == 0 else -seed)
    while True:
        t = (s % 127773) * 0x41A7 - (s // 127773) * 0xB14
        s = t if t > 0 else t + 0x7FFFFFFF
        yield s & 0xFF


def decrypt(data):
    """Returns (seed, plaintext) of a Rand2-encrypted file."""
    seed = struct.unpack_from("<i", data)[0]
    ks = _rand2_stream(seed)
    return seed, bytes(b ^ next(ks) for b in data[4:])


def encrypt(seed, plain):
    ks = _rand2_stream(seed)
    return struct.pack("<i", seed) + bytes(b ^ next(ks) for b in plain)


# ---------------------------------------------------------------- archive header


class FileEntry:
    __slots__ = ("offset", "name_idx", "dir_idx", "size", "inflated")

    def __init__(self, offset, name_idx, dir_idx, size, inflated):
        self.offset, self.name_idx, self.dir_idx = offset, name_idx, dir_idx
        self.size, self.inflated = size, inflated


class ArkHeader:
    """Version-5 archive header. `path(e)` gives "dir/name" of an entry."""

    def __init__(self, raw):
        self.seed, d = decrypt(raw)
        p = 0

        def u32():
            nonlocal p
            v = struct.unpack_from("<I", d, p)[0]
            p += 4
            return v

        self.version = u32()
        if self.version != 5:
            raise ValueError(f"unsupported ark header version {self.version}")
        self.num_arkfiles = u32()
        self.part_sizes = [u32() for _ in range(u32())]
        self.part_names = []
        for _ in range(u32()):
            n = u32()
            self.part_names.append(d[p:p + n].decode("latin1"))
            p += n
        n = u32()
        self.blob = bytearray(d[p:p + n])
        p += n
        self.string_offsets = [u32() for _ in range(u32())]
        self.entries = []
        for _ in range(u32()):
            self.entries.append(FileEntry(*struct.unpack_from("<QIIII", d, p)))
            p += 24
        if p != len(d):
            raise ValueError(f"{len(d) - p} trailing bytes in ark header")

    def string(self, idx):
        o = self.string_offsets[idx]
        return self.blob[o:self.blob.index(b"\0", o)].decode("latin1")

    def path(self, e):
        return f"{self.string(e.dir_idx)}/{self.string(e.name_idx)}"

    def find(self, path):
        for e in self.entries:
            if self.path(e) == path:
                return e
        raise KeyError(path)

    def locate(self, e):
        """Returns (part index, offset inside the part) of an entry."""
        off = e.offset
        for i, size in enumerate(self.part_sizes):
            if off < size:
                return i, off
            off -= size
        raise ValueError("entry offset past the last ark part")

    def add_part(self, name, size):
        """Appends an ark part; returns the global offset where it starts."""
        start = sum(self.part_sizes)
        self.part_sizes.append(size)
        self.part_names.append(name)
        self.num_arkfiles = len(self.part_sizes)
        return start

    def serialize(self):
        out = bytearray()

        def u32(v):
            out.extend(struct.pack("<I", v))

        u32(self.version)
        u32(self.num_arkfiles)
        u32(len(self.part_sizes))
        for s in self.part_sizes:
            u32(s)
        u32(len(self.part_names))
        for n in self.part_names:
            b = n.encode("latin1")
            u32(len(b))
            out.extend(b)
        u32(len(self.blob))
        out.extend(self.blob)
        u32(len(self.string_offsets))
        for o in self.string_offsets:
            u32(o)
        u32(len(self.entries))
        for e in self.entries:
            out.extend(struct.pack("<QIIII", e.offset, e.name_idx, e.dir_idx, e.size, e.inflated))
        return encrypt(self.seed, bytes(out))


# ---------------------------------------------------------------- DTA node tree

# Node types (DataType in the Harmonix engine).
INT, FLOAT, VAR, SYMBOL, UNHANDLED, IFDEF, ELSE, ENDIF = 0, 1, 2, 5, 6, 7, 8, 9
ARRAY, COMMAND, STRING, PROPERTY = 0x10, 0x11, 0x12, 0x13
DEFINE, INCLUDE, MERGE, IFNDEF, AUTORUN, UNDEF = 0x20, 0x21, 0x22, 0x23, 0x24, 0x25
_STR_TYPES = {VAR, SYMBOL, IFDEF, STRING, DEFINE, INCLUDE, MERGE, IFNDEF, UNDEF}
_ARRAY_TYPES = {ARRAY, COMMAND, PROPERTY}
# #autorun is a marker node; the command it runs is the next node.
_INT_TYPES = {UNHANDLED, ELSE, ENDIF, AUTORUN}


class Arr(list):
    """A DTA array: list of nodes plus its source line number."""

    def __init__(self, nodes=(), line=0):
        super().__init__(nodes)
        self.line = line


class Node:
    __slots__ = ("type", "value")

    def __init__(self, type_, value):
        self.type, self.value = type_, value

    def __repr__(self):
        return f"Node({self.type:#x}, {self.value!r})"

    def is_sym(self, name=None):
        return self.type == SYMBOL and (name is None or self.value == name)


def sym(name):
    return Node(SYMBOL, name)


def load_dtb(raw):
    """Decrypts and parses a .dtb; returns (seed, top-level Arr)."""
    seed, d = decrypt(raw)
    p = 0

    def take(fmt):
        nonlocal p
        v = struct.unpack_from(fmt, d, p)[0]
        p += struct.calcsize(fmt)
        return v

    def arr():
        n, line = take("<H"), take("<I")
        return Arr((node() for _ in range(n)), line)

    def node():
        nonlocal p
        t = take("<I")
        if t == INT or t in _INT_TYPES:
            return Node(t, take("<i"))
        if t == FLOAT:
            return Node(t, take("<f"))
        if t in _STR_TYPES:
            n = take("<I")
            v = d[p:p + n].decode("latin1")
            p += n
            return Node(t, v)
        if t in _ARRAY_TYPES:
            return Node(t, arr())
        raise ValueError(f"unknown DTA node type {t:#x} at {p - 4}")

    if take("<B") != 1:
        raise ValueError("dtb does not start with 0x01")
    top = arr()
    if p != len(d):
        raise ValueError(f"{len(d) - p} trailing bytes in dtb")
    return seed, top


def dump_dtb(seed, top):
    out = bytearray(b"\x01")

    def arr(a):
        out.extend(struct.pack("<HI", len(a), getattr(a, "line", 0)))
        for n in a:
            node(n)

    def node(n):
        out.extend(struct.pack("<I", n.type))
        if n.type == INT or n.type in _INT_TYPES:
            out.extend(struct.pack("<i", n.value))
        elif n.type == FLOAT:
            out.extend(struct.pack("<f", n.value))
        elif n.type in _STR_TYPES:
            b = n.value.encode("latin1")
            out.extend(struct.pack("<I", len(b)))
            out.extend(b)
        elif n.type in _ARRAY_TYPES:
            arr(n.value)
        else:
            raise ValueError(f"cannot serialize node type {n.type:#x}")

    arr(top)
    return encrypt(seed, bytes(out))


# ---------------------------------------------------------------- DTA text

_TOKEN = re.compile(r"""
    (?P<ws>\s+|;[^\n]*)
  | (?P<open>[({\[])
  | (?P<close>[)}\]])
  | (?P<str>"[^"]*")
  | (?P<quoted>'[^']*')
  | (?P<directive>\#(?:ifdef|ifndef|define|include|merge|undef|autorun|else|endif))
  | (?P<atom>[^\s()\[\]{}"';]+)
""", re.X)
_OPEN = {"(": ARRAY, "{": COMMAND, "[": PROPERTY}
_CLOSE = {ARRAY: ")", COMMAND: "}", PROPERTY: "]"}


def parse_dta(text):
    """Parses DTA source into a top-level Arr (enough for hand-written snippets)."""
    stack = [(None, Arr(line=1))]
    pending = None  # directive waiting for its argument
    line = 1
    pos = 0
    while pos < len(text):
        m = _TOKEN.match(text, pos)
        if not m:
            raise ValueError(f"DTA syntax error at line {line}: {text[pos:pos + 20]!r}")
        pos = m.end()
        kind, tok = m.lastgroup, m.group()
        if kind == "ws":
            line += tok.count("\n")
            continue
        cur = stack[-1][1]
        if kind == "open":
            stack.append((_OPEN[tok], Arr(line=line)))
            continue
        if kind == "close":
            t, a = stack.pop()
            if t is None or _CLOSE[t] != tok:
                raise ValueError(f"unbalanced {tok!r} at line {line}")
            stack[-1][1].append(Node(t, a))
            continue
        if kind == "directive":
            if tok in ("#else", "#endif", "#autorun"):
                cur.append(Node({"#else": ELSE, "#endif": ENDIF, "#autorun": AUTORUN}[tok], 0))
            else:
                pending = tok
            continue
        if pending:
            t = {"#ifdef": IFDEF, "#ifndef": IFNDEF, "#define": DEFINE, "#include": INCLUDE,
                 "#merge": MERGE, "#undef": UNDEF}[pending]
            cur.append(Node(t, tok))
            pending = None
            continue
        if kind == "str":
            cur.append(Node(STRING, tok[1:-1].replace("\\q", '"')))
        elif kind == "quoted":
            cur.append(Node(SYMBOL, tok[1:-1].replace("\\q", "'")))
        elif tok == "kDataUnhandled":
            cur.append(Node(UNHANDLED, 0))
        elif tok.startswith("$"):
            cur.append(Node(VAR, tok[1:]))
        elif re.fullmatch(r"-?\d+", tok):
            cur.append(Node(INT, int(tok)))
        elif re.fullmatch(r"-?(\d+\.\d*|\.\d+)([eE]-?\d+)?", tok):
            cur.append(Node(FLOAT, float(tok)))
        else:
            cur.append(Node(SYMBOL, tok))
    if len(stack) != 1:
        raise ValueError("unbalanced brackets at end of DTA text")
    return stack[0][1]


def _fmt_sym(s):
    return s if re.fullmatch(r"[A-Za-z_][\w.]*", s) else "'" + s + "'"


def format_node(n, ind=0):
    t, v = n.type, n.value
    if t == INT:
        return str(v)
    if t == FLOAT:
        return repr(v)
    if t == UNHANDLED:
        return "kDataUnhandled"
    if t == ELSE:
        return "#else"
    if t == ENDIF:
        return "#endif"
    if t == VAR:
        return "$" + v
    if t == SYMBOL:
        return _fmt_sym(v)
    if t == STRING:
        return '"' + v.replace('"', "\\q") + '"'
    prefix = {IFDEF: "#ifdef ", IFNDEF: "#ifndef ", DEFINE: "#define ", INCLUDE: "#include ",
              MERGE: "#merge ", UNDEF: "#undef "}
    if t in prefix:
        return prefix[t] + v
    if t == AUTORUN:
        return "#autorun"
    return _fmt_arr({ARRAY: "(", COMMAND: "{", PROPERTY: "["}[t],
                    {ARRAY: ")", COMMAND: "}", PROPERTY: "]"}[t], v, ind)


def _fmt_arr(o, c, v, ind):
    parts = [format_node(x, ind + 1) for x in v]
    one = o + " ".join(parts) + c
    if len(one) < 90 and "\n" not in one:
        return one
    if not parts:
        return o + c
    pad = "\n" + "   " * (ind + 1)
    return o + parts[0] + "".join(pad + p for p in parts[1:]) + c


def format_dta(top):
    return "\n".join(format_node(n) for n in top) + "\n"


# ---------------------------------------------------------------- tree helpers


def find_object(top, cls, name):
    """Finds `{new <cls> <name> ...}` at the top level; returns its Arr."""
    for n in top:
        if (n.type == COMMAND and len(n.value) >= 3 and n.value[0].is_sym("new")
                and n.value[1].is_sym(cls) and n.value[2].is_sym(name)):
            return n.value
    raise KeyError(f"{{new {cls} {name}}} not found")


def find_prop(arr, name):
    """Finds the `(name ...)` handler/property array inside an object or array."""
    for n in arr:
        if n.type == ARRAY and n.value and n.value[0].is_sym(name):
            return n.value
    raise KeyError(f"({name} ...) not found")
