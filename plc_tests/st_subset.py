"""Small, strict ST subset interpreter for these offline regression tests only.

Adapted from this repository's ThreeDriveBench test harness. No IEC type/overflow,
task scheduling, libraries, EtherCAT, motor dynamics or hardware validation implied.
Unknown syntax fails; tests execute the delivered FB body, not a separate model.
"""
import copy
import math
import re
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def source(path, tag):
    return ET.parse(ROOT / path).find('.//' + tag).text


def uncomment(text):
    return re.sub(r'//[^\n]*|\(\*.*?\*\)', '', text, flags=re.S)


TOKEN = re.compile(r'\s*(?:([A-Za-z_]\w*#(?:16#[0-9a-fA-F]+|\d+))|'
                   r'(\d+\.\d+|\d+)|([A-Za-z_]\w*)|(:=|<>|<=|>=|\.\.|[+*/=<>.,;:\[\]()\-]))')


class Parser:
    def __init__(self, text):
        self.tokens = []
        text = uncomment(text).strip()
        while text:
            match = TOKEN.match(text)
            if not match:
                raise SyntaxError(text[:80])
            self.tokens.append(next(g for g in match.groups() if g is not None))
            text = text[match.end():].lstrip()
        self.tokens.append('EOF')
        self.pos = 0

    def peek(self):
        return self.tokens[self.pos]

    def take(self, expected=None):
        result = self.peek()
        if expected and result != expected:
            raise SyntaxError((expected, result, self.tokens[self.pos:self.pos+12]))
        self.pos += 1
        return result

    def expression(self, level=0):
        token = self.take()
        if token in ('NOT', '-', '+'):
            node = ('unary', token, self.expression(60))
        elif token == '(':
            node = self.expression()
            self.take(')')
        elif '#' in token:
            number = token.split('#', 1)[1]
            node = ('const', int(number[3:], 16) if number.startswith('16#') else int(number))
        elif token[0].isdigit():
            node = ('const', float(token) if '.' in token else int(token))
        elif token in ('TRUE', 'FALSE'):
            node = ('const', token == 'TRUE')
        elif self.peek() == '(':
            self.take('(')
            args = []
            if self.peek() != ')':
                args = [self.expression()]
                while self.peek() == ',':
                    self.take(',')
                    args.append(self.expression())
            self.take(')')
            node = ('call', token, args)
        else:
            chain = []
            while self.peek() in ('.', '['):
                if self.take() == '.':
                    chain.append(('field', self.take()))
                else:
                    chain.append(('index', self.expression()))
                    self.take(']')
            node = ('var', token, chain)
        precedence = {'OR': 10, 'AND': 20, '=': 30, '<>': 30, '<': 30, '>': 30,
                      '<=': 30, '>=': 30, '+': 40, '-': 40, '*': 50, '/': 50}
        while precedence.get(self.peek(), -1) >= level:
            operator = self.take()
            node = ('binary', operator, node, self.expression(precedence[operator] + 1))
        return node

    def block(self, stops=('EOF',), case=False):
        result = []
        while self.peek() not in stops:
            if case and (self.peek()[0].isdigit() or '#' in self.peek()):
                break
            token = self.peek()
            if token == 'IF':
                branches = []
                self.take()
                while True:
                    condition = self.expression()
                    self.take('THEN')
                    branches.append((condition, self.block(('ELSIF', 'ELSE', 'END_IF'))))
                    if self.peek() != 'ELSIF':
                        break
                    self.take()
                otherwise = []
                if self.peek() == 'ELSE':
                    self.take()
                    otherwise = self.block(('END_IF',))
                self.take('END_IF')
                result.append(('if', branches, otherwise))
            elif token == 'CASE':
                self.take()
                expr = self.expression()
                self.take('OF')
                arms = []
                while self.peek() not in ('ELSE', 'END_CASE'):
                    labels = [self.expression()]
                    while self.peek() == ',':
                        self.take()
                        labels.append(self.expression())
                    self.take(':')
                    arms.append((labels, self.block(('ELSE', 'END_CASE'), case=True)))
                otherwise = []
                if self.peek() == 'ELSE':
                    self.take()
                    otherwise = self.block(('END_CASE',))
                self.take('END_CASE')
                result.append(('case', expr, arms, otherwise))
            else:
                left = self.expression()
                self.take(':=')
                result.append(('assign', left, self.expression()))
            if self.peek() == ';':
                self.take()
        return result


def ref(node, env):
    assert node[0] == 'var'
    parent, key = env, node[1]
    for kind, part in node[2]:
        parent = parent[key]
        key = part if kind == 'field' else value(part, env)
    return parent, key


def value(node, env):
    kind = node[0]
    if kind == 'const':
        return node[1]
    if kind == 'var':
        obj, key = ref(node, env)
        return obj[key]
    if kind == 'unary':
        item = value(node[2], env)
        return not item if node[1] == 'NOT' else (-item if node[1] == '-' else item)
    if kind == 'call':
        args = [value(arg, env) for arg in node[2]]
        if node[1] == 'GETCURTASKINDEXEX':
            return 1
        if node[1] == 'ABS':
            return abs(args[0])
        if node[1] == 'MAX':
            return max(args)
        if node[1] == 'MIN':
            return min(args)
        if node[1] == 'SQRT':
            return math.sqrt(args[0])
        if node[1] in ('DINT_TO_LREAL', 'UDINT_TO_LREAL'):
            return float(args[0])
        if node[1] == 'UINT_TO_UDINT':
            return int(args[0])
        if node[1] in ('LREAL_TO_DINT', 'LREAL_TO_UDINT'):
            result = round(args[0])
            lo, hi = (0, 4294967295) if node[1].endswith('_UDINT') else (-2147483648, 2147483647)
            assert lo <= result <= hi, ('conversion overflow', result)
            return result
        raise NotImplementedError(node[1])
    op, a, b = node[1], value(node[2], env), value(node[3], env)
    if op == '+': return a + b
    if op == '-': return a - b
    if op == '*': return a * b
    if op == '/': return a / b
    if op == 'AND': return a & b
    if op == 'OR': return a | b
    if op == '=': return a == b
    if op == '<>': return a != b
    if op == '<': return a < b
    if op == '>': return a > b
    if op == '<=': return a <= b
    if op == '>=': return a >= b
    raise NotImplementedError(op)


def execute(block, env):
    for node in block:
        if node[0] == 'assign':
            obj, key = ref(node[1], env)
            obj[key] = copy.deepcopy(value(node[2], env))
        elif node[0] == 'if':
            for cond, body in node[1]:
                if value(cond, env):
                    execute(body, env)
                    break
            else:
                execute(node[2], env)
        elif node[0] == 'case':
            selector = value(node[1], env)
            for labels, body in node[2]:
                if selector in [value(label, env) for label in labels]:
                    execute(body, env)
                    break
            else:
                execute(node[3], env)
        else:
            raise NotImplementedError(node[0])


def fields(body):
    env = {}
    for part in uncomment(body).split(';'):
        if not part.strip():
            continue
        names, definition = part.strip().split(':', 1)
        typ, sep, default = definition.partition(':=')
        initial = value(Parser(default).expression(), {}) if sep else zero(typ.strip())
        for name in names.split(','):
            env[name.strip()] = copy.deepcopy(initial)
    return env


def zero(typ):
    array = re.fullmatch(r'ARRAY\[(\d+)\.\.(\d+)\] OF (\w+)', typ)
    if array:
        lo, hi = int(array[1]), int(array[2])
        return {i: zero(array[3]) for i in range(lo, hi + 1)}
    if typ.startswith('ST_'):
        declaration = source('PLC/DUTs/' + typ + '.TcDUT', 'Declaration')
        return fields(declaration.split('STRUCT', 1)[1].split('END_STRUCT')[0])
    if typ == 'BOOL': return False
    if typ == 'LREAL': return 0.0
    if typ in ('UINT', 'DINT', 'UDINT', 'SINT'): return 0
    raise NotImplementedError(typ)
