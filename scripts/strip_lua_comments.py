#!/usr/bin/env python3
"""Strip all comments from Lua 5.4 source text.

Handles line comments, block comments (--[[ ]] and --[==[ ]==]), long strings
and normal strings so that -- inside string literals is never mistaken for a
comment. Comment-only lines are removed whole; a line that keeps code after
comment removal just loses the comment. Token streams of input and output are
compared to prove no code was touched.
"""
import sys


def _level(s, i):
    if i < len(s) and s[i] == '[':
        n = 0
        while i + 1 + n < len(s) and s[i + 1 + n] == '=':
            n += 1
        if i + 1 + n < len(s) and s[i + 1 + n] == '[':
            return n + 1
    return None


def strip_text(s):
    """Return (stripped_text, tokens_in, tokens_out)."""
    toks_in = []
    out_parts = []
    i = 0
    n = len(s)
    line = []
    saw_code = False
    had_comment = False
    while i < n:
        c = s[i]
        if c == '-' and i + 1 < n and s[i + 1] == '-':
            lvl = _level(s, i + 2)
            if lvl is None:
                while i < n and s[i] != '\n':
                    i += 1
                had_comment = True
                if line and line[-1] != ' ':
                    line.append(' ')
                continue
            close = ']' + '=' * (lvl - 1) + ']'
            j = s.find(close, i + 3 + lvl)
            if j == -1:
                raise ValueError('unterminated block comment')
            i = j + len(close)
            had_comment = True
            if line and line[-1] != ' ':
                line.append(' ')
            continue
        if c == '[':
            lvl = _level(s, i + 1)
            if lvl is not None:
                close = ']' + '=' * (lvl - 1) + ']'
                j = s.find(close, i + 1 + lvl)
                if j == -1:
                    raise ValueError('unterminated long string')
                token = s[i:j + len(close)]
                toks_in.append(token)
                line.append(token)
                saw_code = True
                i = j + len(close)
                continue
        if c == '"' or c == "'":
            j = i + 1
            while j < n:
                if s[j] == '\\':
                    j += 2
                    continue
                if s[j] == c:
                    j += 1
                    break
                if s[j] == '\n':
                    raise ValueError('unterminated string literal')
                j += 1
            else:
                raise ValueError('unterminated string literal')
            token = s[i:j]
            toks_in.append(token)
            line.append(token)
            saw_code = True
            i = j
            continue
        if c.isdigit() or (c == '.' and i + 1 < n and s[i + 1].isdigit()):
            j = i
            if c == '0' and i + 1 < n and s[i + 1] in 'xX':
                j = i + 2
                while j < n and s[j] in '0123456789abcdefABCDEF':
                    j += 1
            else:
                while j < n and (s[j].isdigit() or s[j] == '.'):
                    j += 1
                if j < n and s[j] in 'eE':
                    k = j + 1
                    if k < n and s[k] in '+-':
                        k += 1
                    if k < n and s[k].isdigit():
                        j = k
                        while j < n and s[j].isdigit():
                            j += 1
            token = s[i:j]
            toks_in.append(token)
            line.append(token)
            saw_code = True
            i = j
            continue
        if c.isalpha() or c == '_':
            j = i
            while j < n and (s[j].isalnum() or s[j] == '_'):
                j += 1
            token = s[i:j]
            toks_in.append(token)
            line.append(token)
            saw_code = True
            i = j
            continue
        if c == '\n':
            if saw_code:
                out_parts.append(''.join(line).rstrip() + '\n')
            elif not had_comment:
                out_parts.append('\n')
            line = []
            saw_code = False
            had_comment = False
            i += 1
            continue
        if c.isspace():
            line.append(c)
            i += 1
            continue
        j = i
        while j < n and (s[j] not in ' \t\r\n' and not (s[j] == '-' and j + 1 < n and s[j + 1] == '-')
                         and not s[j].isdigit() and not (s[j].isalpha() or s[j] == '_')
                         and not (s[j] == '"' or s[j] == "'")):
            j += 1
        token = s[i:j]
        toks_in.append(token)
        line.append(token)
        saw_code = True
        i = j
        continue
    if saw_code or line:
        out_parts.append(''.join(line).rstrip())
    return ''.join(out_parts), toks_in


def tokens_of(text):
    """Extract significant tokens purely for comparison."""
    return strip_text(text)[1]


def strip(s):
    out, toks = strip_text(s)
    out_toks = strip_text(out)[1]
    if toks != out_toks:
        raise ValueError('token stream changed - refusing')
    return out


def main():
    if sys.argv[1] == '--verify':
        a, b = sys.argv[2], sys.argv[3]
        with open(a, 'rb') as f:
            src = f.read().decode('utf-8')
        with open(b, 'rb') as f:
            dst = f.read().decode('utf-8')
        ta = tokens_of(src)
        tb = tokens_of(dst)
        if ta != tb:
            for x, y in zip(ta, tb):
                if x != y:
                    print(f'FIRST DIFF: {x!r} vs {y!r}')
                    break
            print(f'FAIL token count {len(ta)} vs {len(tb)}')
            return 1
        print(f'OK {len(ta)} tokens identical')
        return 0
    for path in sys.argv[1:]:
        with open(path, 'rb') as f:
            src = f.read().decode('utf-8')
        out = strip(src)
        with open(path, 'wb') as f:
            f.write(out.encode('utf-8'))
    return 0


if __name__ == '__main__':
    sys.exit(main())
