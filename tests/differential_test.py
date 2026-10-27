#!/usr/bin/env python3
"""Seeded scalar differential tests with a full-i64 native observation channel.

The oracle below is independent Python integer arithmetic with explicit i64
checks, not the Niyah interpreter. All generated programs are typed and bounded.
Failures retain their source, expected outcome and process observations outside
of the repository, plus a greedy, budget-limited reduction of the first failure.
The reducer finds a smaller reproducer, not a mathematically minimal program.
"""
from __future__ import annotations

import argparse
from collections import Counter
from dataclasses import dataclass, replace
import hashlib
import json
import os
from pathlib import Path
import random
import re
import stat
import subprocess
import sys
import tempfile
import time

MIN, MAX, MASK = -(1 << 63), (1 << 63) - 1, (1 << 64) - 1
INT, BOOL = "int", "bool"
GLOBAL, LOCAL, COUNTER = "أثر", "س", "عداد"


class OracleTrap(Exception):
    pass


def checked(value):
    if not MIN <= value <= MAX:
        raise OracleTrap("overflow")
    return value


def signed(bits):
    bits &= MASK
    return bits if bits < (1 << 63) else bits - (1 << 64)


def operation(op, a, b):
    if op == "+": return checked(a + b)
    if op == "-": return checked(a - b)
    if op == "*": return checked(a * b)
    if op in ("/", "%"):
        if not b: raise OracleTrap("division_zero" if op == "/" else "modulo_zero")
        if a == MIN and b == -1: raise OracleTrap("overflow")
        q = abs(a) // abs(b)
        if (a < 0) != (b < 0): q = -q
        return q if op == "/" else a - q * b
    if op in ("<<", ">>"):
        if not 0 <= b <= 63: raise OracleTrap("shift_count")
        return checked(a * (1 << b)) if op == "<<" else a >> b
    if op == "&": return signed((a & MASK) & (b & MASK))
    if op == "|": return signed((a & MASK) | (b & MASK))
    if op == "^": return signed((a & MASK) ^ (b & MASK))
    if op == "==": return a == b
    if op == "!=": return a != b
    if op == "<": return a < b
    if op == "<=": return a <= b
    if op == ">": return a > b
    if op == ">=": return a >= b
    raise ValueError(op)


@dataclass(frozen=True)
class Expr:
    kind: str
    type: str = INT
    value: object = None
    args: tuple = ()


@dataclass(frozen=True)
class Stmt:
    kind: str
    name: str = ""
    expr: Expr | None = None
    body: tuple = ()
    other: tuple = ()
    count: int = 0


@dataclass(frozen=True)
class Program:
    body: tuple
    result: Expr
    category: str


def lit(value): return Expr("literal", BOOL if isinstance(value, bool) else INT, value)
def var(name=LOCAL): return Expr("variable", INT, name)
def binary(op, a, b):
    return Expr("binary", BOOL if op in ("==", "!=", "<", "<=", ">", ">=", "&&", "||") else INT, op, (a, b))
def unary(op, a): return Expr("unary", BOOL if op == "!" else INT, op, (a,))
def call(tag, value): return Expr("call", INT, tag, (value,))
def assign(name, expr): return Stmt("assign", name, expr)
def let(name, expr): return Stmt("let", name, expr)


def eval_expr(expr, env):
    if expr.kind == "literal": return expr.value
    if expr.kind == "variable": return env[expr.value]
    if expr.kind == "call":
        value = eval_expr(expr.args[0], env)
        env[GLOBAL] = checked(checked(env[GLOBAL] * 10) + expr.value)
        return value
    a = eval_expr(expr.args[0], env)
    if expr.kind == "unary":
        if expr.value == "-": return checked(-a)
        if expr.value == "~": return signed(~(a & MASK))
        if expr.value == "!": return not a
    if expr.kind == "binary":
        if expr.value == "&&": return bool(a) and bool(eval_expr(expr.args[1], env))
        if expr.value == "||": return bool(a) or bool(eval_expr(expr.args[1], env))
        return operation(expr.value, a, eval_expr(expr.args[1], env))
    raise ValueError(expr.kind)


def exec_statements(body, env):
    for stmt in body:
        if stmt.kind in ("let", "assign"):
            env[stmt.name] = eval_expr(stmt.expr, env)
        elif stmt.kind == "expr": eval_expr(stmt.expr, env)
        elif stmt.kind == "if":
            exec_statements(stmt.body if eval_expr(stmt.expr, env) else stmt.other, env)
        elif stmt.kind == "loop":
            for i in range(stmt.count):
                env[COUNTER] = i
                exec_statements(stmt.body, env)
            env.pop(COUNTER, None)
        else: raise ValueError(stmt.kind)


def expected(program):
    env = {GLOBAL: 0}
    try:
        exec_statements(program.body, env)
        return {"kind": "value", "value": eval_expr(program.result, env)}
    except OracleTrap as exc:
        trap = str(exc)
        return {"kind": "trap", "trap": trap,
                "native_exit": {"overflow": 120, "division_zero": 121,
                                "modulo_zero": 121, "shift_count": 122}[trap]}


def valid_expr(expr, names):
    if expr.kind == "literal":
        return isinstance(expr.value, bool) if expr.type == BOOL else isinstance(expr.value, int) and MIN <= expr.value <= MAX
    if expr.kind == "variable": return names.get(expr.value) == expr.type
    if not all(valid_expr(a, names) for a in expr.args): return False
    if expr.kind == "call": return expr.type == INT and expr.args[0].type == INT and 1 <= expr.value <= 3
    if expr.kind == "unary": return expr.args[0].type == (BOOL if expr.value == "!" else INT)
    a, b = expr.args
    if expr.value in ("&&", "||"): return a.type == b.type == BOOL
    if expr.value in ("==", "!="): return a.type == b.type
    return a.type == b.type == INT


def valid_body(body, names, nested=False):
    for stmt in body:
        if stmt.expr is not None and not valid_expr(stmt.expr, names): return False
        if stmt.kind == "let":
            if nested or stmt.name in names: return False
            names[stmt.name] = stmt.expr.type
        elif stmt.kind == "assign":
            if names.get(stmt.name) != stmt.expr.type: return False
        elif stmt.kind == "if":
            if stmt.expr.type != BOOL: return False
            if not valid_body(stmt.body, dict(names), True) or not valid_body(stmt.other, dict(names), True): return False
        elif stmt.kind == "loop":
            if nested or not 0 <= stmt.count <= 7: return False
            scope = dict(names); scope[COUNTER] = INT
            if not valid_body(stmt.body, scope, True): return False
        elif stmt.kind != "expr": return False
    return True


def validate(program):
    names = {GLOBAL: INT}
    return valid_body(program.body, names) and program.result.type == INT and valid_expr(program.result, names)


def expr_source(expr):
    if expr.kind == "literal":
        if expr.type == BOOL: return "(0 == 0)" if expr.value else "(0 != 0)"
        if expr.value == MIN: return "(-9223372036854775807 - 1)"
        return str(expr.value) if expr.value >= 0 else f"({expr.value})"
    if expr.kind == "variable": return expr.value
    if expr.kind == "call": return f"وقع({expr.value}, {expr_source(expr.args[0])})"
    if expr.kind == "unary": return f"({expr.value}{expr_source(expr.args[0])})"
    return f"({expr_source(expr.args[0])} {expr.value} {expr_source(expr.args[1])})"


def statements_source(body):
    out = []
    for stmt in body:
        if stmt.kind == "let":
            ty = "منطقي" if stmt.expr.type == BOOL else "صحيح"
            out.append(f"متغير {stmt.name}: {ty} = {expr_source(stmt.expr)};")
        elif stmt.kind == "assign": out.append(f"{stmt.name} = {expr_source(stmt.expr)};")
        elif stmt.kind == "expr": out.append(expr_source(stmt.expr) + ";")
        elif stmt.kind == "if":
            out.append(f"إذا ({expr_source(stmt.expr)}) {{ {statements_source(stmt.body)} }} وإلا {{ {statements_source(stmt.other)} }}")
        elif stmt.kind == "loop":
            out.append(f"{{ متغير {COUNTER}: صحيح = 0; طالما ({COUNTER} < {stmt.count}) {{ {statements_source(stmt.body)} {COUNTER} = {COUNTER} + 1; }} }}")
    return " ".join(out)


def expressions(program):
    def walk_expr(expr):
        yield expr
        for arg in expr.args: yield from walk_expr(arg)
    def walk_body(body):
        for stmt in body:
            if stmt.expr is not None: yield from walk_expr(stmt.expr)
            yield from walk_body(stmt.body)
            yield from walk_body(stmt.other)
    yield from walk_body(program.body)
    yield from walk_expr(program.result)


def source(program):
    nodes = list(expressions(program))
    needs_call = any(e.kind == "call" for e in nodes)
    needs_global = needs_call or any(e.kind == "variable" and e.value == GLOBAL for e in nodes)
    prefix = "متغير أثر: صحيح = 0;\n" if needs_global else ""
    if needs_call:
        prefix += "دالة وقع(وسم: صحيح, قيمة: صحيح) -> صحيح { أثر = أثر * 10 + وسم; أرجع قيمة; }\n"
    return prefix + "دالة رئيسية() -> صحيح { " + statements_source(program.body) + " أرجع " + expr_source(program.result) + "; }\n"


POSITIVES = [0, 1, 2, (1 << 31) - 1, 1 << 31, 1 << 32, 1 << 40, MAX]
BOUNDARIES = [MIN, -(1 << 40), -(1 << 31) - 1, -(1 << 31), -7, -1, 0, 1, 7, (1 << 31) - 1, 1 << 31, 1 << 40, MAX]
CATEGORIES = ["literal_rhs", "comparisons", "negative_rhs_general", "mixed_tree", "shifts", "division_remainder", "ordered_side_effects", "short_circuit", "bounded_loop", "signed_extremes"]


def random_expr(rng, depth):
    if depth == 0:
        return rng.choice([lit(rng.choice(BOUNDARIES)), var(), call(rng.randint(1, 3), lit(rng.randint(-20, 20)))])
    op = rng.choice(["+", "-", "*", "&", "|", "^", "/", "%", "<<", ">>"])
    left = random_expr(rng, depth - 1)
    if op in ("<<", ">>"): right = lit(rng.choice([0, 1, 2, 31, 32, 63]))
    elif rng.randrange(2): right = lit(rng.choice(BOUNDARIES))
    else: right = random_expr(rng, depth - 1)
    return binary(op, left, right)


def generate(rng, index):
    category = CATEGORIES[index % len(CATEGORIES)]
    n = index // len(CATEGORIES)
    a, b = lit(rng.choice(BOUNDARIES)), lit(POSITIVES[n % len(POSITIVES)])
    if category == "literal_rhs":
        result = binary(["+", "-", "*", "&", "|", "^"][n % 6], call(1, a) if n % 3 == 0 else a, b)
        return Program((), result, category)
    if category == "comparisons":
        cond = binary(["==", "!=", "<", "<=", ">", ">="][n % 6], a, b)
        body = (let(LOCAL, lit(0)), Stmt("if", expr=cond,
                body=(assign(LOCAL, lit((1 << 40) + n)),), other=(assign(LOCAL, lit(-(1 << 40) + n)),)))
        return Program(body, var(), category)
    if category == "negative_rhs_general":
        negative = [-1, -2, -(1 << 31), -(1 << 31) - 1, -(1 << 32), -(1 << 40), MIN][n % 7]
        return Program((), binary(["+", "-", "*", "&", "|", "^", "/", "%"][n % 8], a, lit(negative)), category)
    if category == "mixed_tree":
        return Program((let(LOCAL, lit(rng.randint(-50, 50))),), random_expr(rng, 2 + n % 3), category)
    if category == "shifts":
        left = [0, 1, -1, MIN, MAX, 1 << 31, -(1 << 31), 1 << 40, -16][n % 9]
        count = [-1, 0, 1, 31, 32, 63, 64][n % 7]
        return Program((), binary("<<" if n % 2 == 0 else ">>", lit(left), lit(count)), category)
    if category == "division_remainder":
        left = [MIN, MIN, MAX, -7, 7, -7, MAX, 1 << 40][n % 8]
        right = [-1, 0, 1, 3, -3, 1 << 31, (1 << 31) + 1, 1 << 32][n % 8]
        return Program((), binary("/" if n % 2 == 0 else "%", lit(left), lit(right)), category)
    if category == "ordered_side_effects":
        left = binary("*", call(1, lit(n % 19 + 1)), lit(1 << 31))
        mixed = binary("+", binary("&", left, lit(MAX)), call(2, lit(-n)))
        result = binary("+", mixed, binary("*", var(GLOBAL), lit(1 << 40)))
        return Program((), result, category)
    if category == "short_circuit":
        trap = binary("==", binary("/", lit(1), lit(0)), lit(0))
        effect1 = binary("==", call(1, lit(7)), lit(7))
        effect2 = binary("==", call(2, lit(7)), lit(7))
        variants = [binary("&&", lit(False), trap), binary("||", lit(True), trap),
                    binary("&&", lit(False), effect1), binary("||", lit(True), effect1),
                    binary("&&", lit(True), effect1), binary("||", lit(False), effect2),
                    binary("&&", unary("!", effect1), effect2), binary("||", effect1, effect2)]
        body = (let(LOCAL, lit(0)), Stmt("if", expr=variants[n % 8],
                body=(assign(LOCAL, lit(17)),), other=(assign(LOCAL, lit(29)),)))
        return Program(body, binary("+", var(), binary("*", var(GLOBAL), lit(1 << 40))), category)
    if category == "bounded_loop":
        cond = binary("==", binary("&", var(COUNTER), lit(1)), lit(0))
        branch = Stmt("if", expr=cond, body=(assign(LOCAL, binary("+", var(), lit((1 << 31) - 1))),),
                      other=(assign(LOCAL, binary("-", var(), lit((1 << 31) + 1))),))
        loop = Stmt("loop", count=n % 7, body=(branch, Stmt("expr", expr=call(1, lit(0)))))
        return Program((let(LOCAL, lit(-(1 << 40) + n)), loop), binary("+", var(), binary("*", var(GLOBAL), lit(256))), category)
    extremes = [binary("+", lit(MAX), lit(1)), binary("-", lit(MIN), lit(1)),
                binary("*", lit(MAX), lit(2)), unary("-", lit(MIN)),
                binary("/", lit(MIN), lit(-1)), binary("%", lit(MIN), lit(-1)),
                binary("<<", lit(1), lit(63)), binary("<<", lit(MIN), lit(1)),
                binary("+", lit(MAX), lit(0)), binary("-", lit(MIN), lit(0)),
                binary("*", lit(MAX), lit(1)), binary("*", lit(MIN), lit(1)),
                binary(">>", lit(MIN), lit(63)), binary("<<", lit(-1), lit(63)),
                unary("~", lit(MIN)), unary("~", lit(MAX))]
    return Program((), extremes[n % len(extremes)], category)


def oracle_controls():
    checks = [operation("/", -7, 3) == -2, operation("%", -7, 3) == -1,
              operation("%", 7, -3) == 1, operation(">>", MIN, 63) == -1,
              operation("<<", -1, 63) == MIN, operation("^", MIN, MAX) == -1,
              operation("|", MIN, 0) == MIN, operation("&", -1, MAX) == MAX,
              operation("+", 1 << 40, 42) == (1 << 40) + 42]
    for op, a, b, trap in [("+", MAX, 1, "overflow"), ("-", MIN, 1, "overflow"),
                          ("*", MIN, -1, "overflow"), ("/", MIN, -1, "overflow"),
                          ("%", MIN, -1, "overflow"), ("/", 1, 0, "division_zero"),
                          ("<<", 1, 64, "shift_count"), (">>", 1, -1, "shift_count")]:
        try: operation(op, a, b); checks.append(False)
        except OracleTrap as exc: checks.append(str(exc) == trap)
    if not all(checks): raise AssertionError("independent oracle control failed")
    return len(checks)


def sha(path): return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def invoke(argv, timeout):
    start = time.monotonic()
    try:
        p = subprocess.run([str(x) for x in argv], capture_output=True, timeout=timeout)
        return {"argv": [str(x) for x in argv], "returncode": p.returncode,
                "stdout": p.stdout.decode("utf-8", "backslashreplace"),
                "stderr": p.stderr.decode("utf-8", "backslashreplace"),
                "seconds": round(time.monotonic() - start, 6)}
    except subprocess.TimeoutExpired as exc:
        return {"argv": [str(x) for x in argv], "returncode": None, "timeout": True,
                "stdout": (exc.stdout or b"").decode("utf-8", "backslashreplace"),
                "stderr": (exc.stderr or b"").decode("utf-8", "backslashreplace"),
                "seconds": round(time.monotonic() - start, 6)}
    except OSError as exc:
        return {"argv": [str(x) for x in argv], "returncode": None,
                "spawn_error": f"{type(exc).__name__}: {exc}",
                "stdout": "", "stderr": "",
                "seconds": round(time.monotonic() - start, 6)}


def run_case(program, folder, tools, timeout):
    folder.mkdir()
    text = source(program)
    src, executable = folder / "program.nya", folder / "program.elf"
    src.write_text(text, encoding="utf-8")
    want = expected(program)
    compiler = invoke([tools["compiler"], "-o", executable, src], timeout)
    result = {"category": program.category, "source_sha256": sha(src), "expected": want, "compiler": compiler}
    if compiler["returncode"] != 0 or compiler["stdout"] or compiler["stderr"]:
        result["failure"] = "compiler"
    else:
        try:
            metadata = executable.stat()
            if not stat.S_ISREG(metadata.st_mode):
                raise OSError("compiler output is not a regular file")
            artifact_hash = sha(executable)
            if not os.access(executable, os.X_OK):
                raise OSError("compiler output is not executable")
            result.update(executable_sha256=artifact_hash, executable_bytes=metadata.st_size)
        except OSError as exc:
            result["failure"] = "compiler_artifact"
            result["artifact_error"] = f"{type(exc).__name__}: {exc}"
    if "failure" not in result:
        interpreter = invoke([tools["interpreter"], src], timeout)
        native = invoke([tools["probe"], executable], timeout)
        result.update(interpreter=interpreter, native=native)
        if want["kind"] == "value":
            line = f"RESULT={want['value']}\n"
            if interpreter["returncode"] != 0 or interpreter["stdout"] != line or interpreter["stderr"]:
                result["failure"] = "interpreter_value"
            elif native["returncode"] != 0 or native["stdout"] != line or native["stderr"]:
                result["failure"] = "native_value"
        else:
            trap = want["trap"]
            interpreter_message = {"overflow": "integer overflow", "division_zero": "division by zero",
                                   "modulo_zero": "modulo by zero", "shift_count": "invalid shift count"}[trap]
            native_message = {"overflow": "runtime: signed arithmetic overflow\n", "division_zero": "runtime: division by zero\n",
                              "modulo_zero": "runtime: division by zero\n", "shift_count": "runtime: shift count outside 0..63\n"}[trap]
            diagnostic = rf"evaluation: [1-9][0-9]*:[1-9][0-9]* {re.escape(interpreter_message)}\n"
            if interpreter["returncode"] != 3 or interpreter["stdout"] or re.fullmatch(diagnostic, interpreter["stderr"]) is None:
                result["failure"] = "interpreter_trap"
            elif native["returncode"] != 0 or native["stdout"] != f"RESULT={want['native_exit']}\n" or native["stderr"] != native_message:
                result["failure"] = "native_trap"
    result.setdefault("failure", None)
    (folder / "result.json").write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    return result


def expr_reductions(expr):
    for simple in ([False, True] if expr.type == BOOL else [0, 1, -1]):
        if expr != lit(simple): yield lit(simple)
    for arg in expr.args:
        if arg.type == expr.type: yield arg
    if expr.kind == "literal" and expr.type == INT and abs(expr.value) > 1:
        yield lit(expr.value // 2)
    for i, arg in enumerate(expr.args):
        for reduced in expr_reductions(arg):
            args = list(expr.args); args[i] = reduced
            yield replace(expr, args=tuple(args))


def body_reductions(body):
    for i, stmt in enumerate(body):
        yield body[:i] + body[i + 1:]
        if stmt.expr is not None:
            for expr in expr_reductions(stmt.expr):
                yield body[:i] + (replace(stmt, expr=expr),) + body[i + 1:]
        if stmt.kind == "loop" and stmt.count:
            for count in sorted({0, 1, stmt.count // 2} - {stmt.count}):
                yield body[:i] + (replace(stmt, count=count),) + body[i + 1:]
        for field in ("body", "other"):
            for reduced in body_reductions(getattr(stmt, field)):
                yield body[:i] + (replace(stmt, **{field: reduced}),) + body[i + 1:]


def reduce_failure(program, initial, folder, tools, timeout, budget):
    folder.mkdir()
    current, current_result = program, initial
    signature = (initial["failure"], initial["expected"]["kind"], initial["expected"].get("trap"))
    attempts, accepted = 0, 0
    seen = {source(program)}
    deadline = time.monotonic() + 30
    while attempts < budget and time.monotonic() < deadline:
        proposals = (replace(current, result=e) for e in expr_reductions(current.result))
        bodies = (replace(current, body=b) for b in body_reductions(current.body))
        found = False
        for candidate in (*proposals, *bodies):
            text = source(candidate)
            if text in seen or len(text.encode()) >= len(source(current).encode()) or not validate(candidate): continue
            seen.add(text)
            if attempts >= budget or time.monotonic() >= deadline: break
            attempts += 1
            result = run_case(candidate, folder / f"attempt-{attempts:03d}", tools, timeout)
            key = (result["failure"], result["expected"]["kind"], result["expected"].get("trap"))
            if key == signature:
                current, current_result = candidate, result
                accepted += 1; found = True; break
        if not found: break
    (folder / "minimized.nya").write_text(source(current), encoding="utf-8")
    report = {"attempts": attempts, "accepted_reductions": accepted,
              "original_source_bytes": len(source(program).encode()), "minimized_source_bytes": len(source(current).encode()),
              "limitation": "Greedy source-size reduction; no new attempts after 30 seconds; in-flight subprocess timeouts still apply; bounded attempts, not globally minimal.",
              "result": current_result}
    (folder / "reduction.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    return {k: v for k, v in report.items() if k != "result"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--seed", type=int, default=20261027)
    parser.add_argument("--cases", type=int, default=200)
    parser.add_argument("--evidence-dir", type=Path)
    parser.add_argument("--timeout", type=float, default=10)
    parser.add_argument("--shrink-attempts", type=int, default=80)
    args = parser.parse_args()
    if not 1 <= args.cases <= 10000 or not 0 < args.timeout <= 60 or not 0 <= args.shrink_attempts <= 500:
        parser.error("cases must be 1..10000, timeout 0..60, shrink-attempts 0..500")
    repo = Path(__file__).resolve().parent.parent
    tools = {"compiler": Path(os.environ.get("NIYAHC", repo / "niyahc")).resolve(),
             "interpreter": Path(os.environ.get("NIYAHRUN", repo / "niyahrun")).resolve(),
             "probe": Path(os.environ.get("NATIVE_RESULT_PROBE", repo / "tests/native_result_probe")).resolve()}
    for name, path in tools.items():
        if not path.is_file() or not os.access(path, os.X_OK): parser.error(f"{name} is not executable: {path}")
    if args.evidence_dir: args.evidence_dir.mkdir(parents=True, exist_ok=True)
    evidence = Path(tempfile.mkdtemp(prefix=f"seed-{args.seed}-", dir=args.evidence_dir))
    identities = {name: {"path": str(path), "sha256": sha(path)} for name, path in tools.items()}
    identities["runner"] = {"path": str(Path(__file__).resolve()), "sha256": sha(__file__)}
    controls = oracle_controls()
    rng = random.Random(args.seed)
    distribution, operators, shapes, outcomes = Counter(), Counter(), Counter(), Counter()
    failures, first_failure, source_hashes = [], None, []
    start = time.monotonic()
    for index in range(args.cases):
        program = generate(rng, index)
        if not validate(program): raise AssertionError(f"generator produced invalid typed IR: {index}")
        distribution[program.category] += 1
        for expr in expressions(program):
            if expr.kind == "binary":
                operators[expr.value] += 1
                right = expr.args[1]
                shape = "nonnegative_A_INT_rhs" if right.kind == "literal" and right.type == INT and 0 <= right.value <= MAX else "general_rhs"
                shapes[shape] += 1
                if shape == "nonnegative_A_INT_rhs": shapes["rhs_fits_signed32" if right.value <= (1 << 31) - 1 else "rhs_requires_full64"] += 1
        result = run_case(program, evidence / f"case-{index:04d}", tools, args.timeout)
        source_hashes.append(result["source_sha256"])
        outcomes[result["expected"].get("trap", "value")] += 1
        if result["failure"]:
            failures.append({"case": index, "category": program.category, "failure": result["failure"]})
            if first_failure is None: first_failure = (program, result)
    reduction = None
    if first_failure and args.shrink_attempts:
        reduction = reduce_failure(*first_failure, evidence / "first-failure-reduction", tools, args.timeout, args.shrink_attempts)
    unchanged = {name: sha(item["path"]) == item["sha256"] for name, item in identities.items()}
    report = {"seed": args.seed, "requested_cases": args.cases, "executed_cases": args.cases,
              "passed_cases": args.cases - len(failures), "failed_cases": len(failures),
              "category_distribution": dict(distribution), "operator_distribution": dict(sorted(operators.items())),
              "rhs_shape_distribution": dict(shapes), "expected_outcomes": dict(outcomes),
              "distribution_scope": "Generated typed IR only; fixed marker-function and loop-counter boilerplate operators are excluded.",
              "oracle_controls_passed": controls, "identities": identities, "inputs_unchanged": unchanged,
              "programs_sha256": hashlib.sha256("\n".join(source_hashes).encode()).hexdigest(),
              "failures": failures, "first_failure_reduction": reduction,
              "elapsed_seconds": round(time.monotonic() - start, 3), "evidence": str(evidence),
              "scope": "Deterministic bounded typed generator; independent checked-i64 oracle plus interpreter and observed full native exit argument. No host sanitizer coverage claim for emitted machine code."}
    (evidence / "summary.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, ensure_ascii=False, sort_keys=True))
    return 0 if not failures and all(unchanged.values()) else 1


if __name__ == "__main__":
    try: raise SystemExit(main())
    except (OSError, ValueError, AssertionError) as exc:
        print(f"differential_test: {exc}", file=sys.stderr)
        raise SystemExit(2)
