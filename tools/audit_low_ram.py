#!/usr/bin/env python3
"""Conservative source audit of nullable values reaching pointer dereferences.

Uses Clang's AST, including casts and indexing. Findings require control-flow
and original-MIPS review: this is an inventory, not proof of null reachability.
It never changes game/decomp source. Temporary compatibility prototypes only
let Clang parse code accepted by the port's permissive GCC front end.
"""
import argparse
import json
from pathlib import Path
import re
import subprocess

from gen_decls import proto

ROOT = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('sources', nargs='+', type=Path)
p.add_argument('--out', required=True, type=Path)
a = p.parse_args()
out = a.out.resolve()
if not out.is_relative_to(ROOT / 'build'):
    p.error('reports must stay under build/')
if out.exists():
    p.error('report already exists')
work = out.with_suffix('.work')
work.mkdir(parents=True, exist_ok=False)
flows, seeds, accesses, coverage = [], {}, [], []
ZERO = 'constant:null'
seeds[ZERO] = [{'reason': 'literal zero'}]

def walk(n):
    yield n
    for child in n.get('inner', []):
        yield from walk(child)

def unwrap(n):
    while n.get('kind') in ('ImplicitCastExpr', 'CStyleCastExpr', 'ParenExpr',
                           'ConstantExpr') and n.get('inner'):
        n = n['inner'][0]
    return n

def point(n):
    r = n.get('range', {}).get('begin', n.get('loc', {}))
    return r.get('expansionLoc', r)

def snippet(n, source):
    r = n.get('range', {})
    start = point(n).get('offset', 0)
    end = r.get('end', {})
    end = end.get('expansionLoc', end)
    return source[start:end.get('offset', start) + end.get('tokLen', 1)]

for index, path in enumerate(a.sources):
    path = path.resolve()
    relative = path.relative_to(ROOT / 'decomp')
    binary = relative.parts[1] if relative.parts[0] in ('src', 'src_equiv') else re.sub(r'_[0-9A-F]+$', '', path.stem)
    header = 'ff9.h' if binary == 'main' else binary + '.h'
    source = path.read_text()
    ast_path = work / f'{index}.ast.json'
    log_path = work / f'{index}.clang.log'
    command = ['clang', '-m32', '-std=gnu89', '-Wno-everything', '-ferror-limit=0',
               '-fsyntax-only', '-DPORT_HEADER="' + header + '"', '-DPORT_BIN_' + binary,
               '-I' + str(ROOT / 'decomp/include'), '-I' + str(ROOT / 'port/include'),
               '-include', str(ROOT / 'port/include/port_prelude.h')]
    command += ['-I' + str(d) for d in (ROOT / 'decomp/src_equiv').glob('*/')]

    def parse(extra=()):
        with ast_path.open('w') as output, log_path.open('w') as log:
            result = subprocess.run(command + list(extra) + ['-Xclang', '-ast-dump=json', str(path)],
                                    stdout=output, stderr=log)
        return result.returncode, json.loads(ast_path.read_text())

    code, tree = parse()
    names = set(re.findall("conflicting types for '([^']+)'", log_path.read_text()))
    temporary_prototypes = [proto(n['name'], n['type']['qualType'])
                            for n in tree.get('inner', [])
                            if n.get('kind') == 'FunctionDecl' and n.get('name') in names
                            and any(c.get('kind') == 'CompoundStmt' for c in n.get('inner', []))]
    if temporary_prototypes:
        prototype_path = work / f'{index}.prototypes.h'
        prototype_path.write_text('\n'.join(temporary_prototypes) + '\n')
        del tree
        code, tree = parse(['-include', str(prototype_path)])
    functions = [n for n in tree.get('inner', []) if n.get('kind') == 'FunctionDecl'
                 and not n.get('loc', {}).get('includedFrom')
                 and any(c.get('kind') == 'CompoundStmt' for c in n.get('inner', []))]
    coverage.append({'source': str(relative), 'clang_exit': code,
                     'compatibility_prototypes': len(temporary_prototypes),
                     'functions': len(functions), 'diagnostics': str(log_path)})
    defined = {n['name'] for n in functions}
    for fn in functions:
        function = binary + ':' + fn['name']
        body = next(c for c in fn['inner'] if c.get('kind') == 'CompoundStmt')
        local_ids = {n['id'] for n in walk(fn) if n.get('kind') in ('ParmVarDecl', 'VarDecl')}

        def keys(n):
            n = unwrap(n)
            kind = n.get('kind')
            if kind in ('GNUNullExpr', 'CXXNullPtrLiteralExpr') or (kind == 'IntegerLiteral' and n.get('value') == '0'):
                return {ZERO}
            if kind == 'DeclRefExpr':
                decl = n.get('referencedDecl', {})
                if decl.get('kind') in ('VarDecl', 'ParmVarDecl'):
                    if decl.get('id') in local_ids:
                        return {function + ':local:' + decl.get('name', '') + ':' + decl.get('id', '')}
                    return {'global:' + decl.get('name', '')}
            if kind == 'MemberExpr':
                base = n.get('inner', [{}])[0]
                typ = base.get('type', {}).get('desugaredQualType', base.get('type', {}).get('qualType', '?'))
                typ = re.sub(r'\b(struct|const|volatile)\s+', '', typ)
                return {'field:' + typ + ':' + n.get('name', '')}
            if kind == 'CallExpr' and n.get('inner'):
                callee = unwrap(n['inner'][0]).get('referencedDecl', {}).get('name')
                if callee:
                    prefix = binary if callee in defined or (callee.startswith('func_801') and binary != 'main') else 'main'
                    return {'return:' + prefix + ':' + callee}
            if kind == 'BinaryOperator' and n.get('opcode') in ('+', '-'):
                return set().union(*(keys(c) for c in n.get('inner', [])
                                     if '*' in c.get('type', {}).get('qualType', '')))
            if kind == 'BinaryOperator' and n.get('opcode') in ('=', ','):
                return keys(n['inner'][-1])
            if kind == 'ConditionalOperator':
                return set().union(*(keys(c) for c in n.get('inner', [])[1:]))
            return set()

        def evidence(n, expression):
            offset = point(n).get('offset', 0)
            return {'source': str(relative), 'function': fn['name'],
                    'line': source.count('\n', 0, offset) + 1, 'expression': expression[:240]}

        for node in walk(body):
            kind, children = node.get('kind'), node.get('inner', [])
            target, values = set(), set()
            if kind == 'VarDecl' and children:
                target = {function + ':local:' + node.get('name', '') + ':' + node['id']}
                values = keys(children[-1])
            elif kind == 'BinaryOperator' and node.get('opcode') == '=' and len(children) == 2:
                target, values = keys(children[0]), keys(children[1])
            elif kind == 'ReturnStmt' and children and '*' in fn.get('type', {}).get('qualType', '').split('(')[0]:
                target, values = {'return:' + function}, keys(children[0])
            if target and values:
                flows.append((target, values, evidence(node, snippet(node, source))))
            base = None
            if kind == 'MemberExpr' and node.get('isArrow') and children:
                base = children[0]
            elif kind == 'UnaryOperator' and node.get('opcode') == '*' and children:
                base = children[0]
            elif kind == 'ArraySubscriptExpr' and children:
                base = next((c for c in children if '*' in c.get('type', {}).get('qualType', '')), None)
            if base is not None:
                accesses.append((keys(base), evidence(node, snippet(node, source))))
    del tree
    ast_path.unlink()
    print(f'audited {relative}: {len(functions)} functions, clang exit {code}', flush=True)

nullable = set(seeds)
for _ in range(len(flows) + 1):
    added = False
    for targets, origins, entry in flows:
        if origins & nullable:
            for target in targets - nullable:
                nullable.add(target)
                seeds[target] = [{**entry, 'from': sorted(origins & nullable)}]
                added = True
    if not added:
        break
findings = [{**entry, 'nullable_values': sorted(values & nullable)}
            for values, entry in accesses if values & nullable]
out.write_text(json.dumps({'scope': 'conservative may-be-null inventory; requires control-flow and original-MIPS review',
                           'coverage': coverage, 'nullable_sources': seeds,
                           'dereferences': findings}, indent=2) + '\n')
print(f'{len(findings)} candidate dereferences; report {out}', flush=True)
