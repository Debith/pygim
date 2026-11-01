"""What a module's .pyi says against what its pybind11 bindings say: the stub an editor and a
type checker read must give the bindings' names, parameters and docstrings (help() shows the
bindings', an editor the stub's: one text). Used by each extension's TestStub."""

import ast
import pathlib


class Stub:
    """The .pyi beside `module` (its last dotted name: code.pyi beside code.cpython-*.so)."""

    NONE_GIVEN = object()

    def __init__(self, module):
        path = pathlib.Path(module.__file__).with_name(module.__name__.rsplit(".", 1)[-1] + ".pyi")
        self.tree = ast.parse(path.read_text(encoding="utf-8"))

    def names(self):
        """Every class, function and annotated name the stub declares at module level."""
        return {n.name for n in self.tree.body if isinstance(n, (ast.ClassDef, ast.FunctionDef))} | {
            n.target.id for n in self.tree.body if isinstance(n, ast.AnnAssign)}

    def function(self, name):
        return next(n for n in self.tree.body if isinstance(n, ast.FunctionDef) and n.name == name)

    def cls(self, name):
        return next(n for n in self.tree.body if isinstance(n, ast.ClassDef) and n.name == name)

    def methods(self, cls_name):
        """A class's defs, properties and annotated attributes (`__hash__: ClassVar[None]`) by name;
        of a def's overloads, the last, which carries the docstring."""
        body = self.cls(cls_name).body
        return {**{a.target.id: a for a in body if isinstance(a, ast.AnnAssign) and isinstance(a.target, ast.Name)},
                **{f.name: f for f in body if isinstance(f, ast.FunctionDef)}}

    @classmethod
    def params(cls, f, method=True):
        """(name, keyword_only, default) of each parameter of a stub's def, self left out; one only
        positional is ("/", ...) after it, as pybind11 marks it, and `*name` is ("*args", ...): pybind11
        names every one so."""
        value = lambda d: cls.NONE_GIVEN if d is None else eval(compile(ast.Expression(d), "<stub>", "eval"), {})
        positional = f.args.posonlyargs + f.args.args
        defaults = [None] * (len(positional) - len(f.args.defaults)) + f.args.defaults
        out = []
        for i, (a, d) in enumerate(zip(positional, defaults)):
            if not (method and i == 0):
                out.append((a.arg, False, value(d)))
            if i == len(f.args.posonlyargs) - 1:
                out.append(("/", False, cls.NONE_GIVEN))
        if f.args.vararg:
            out.append(("*args", False, cls.NONE_GIVEN))
        return out + [(a.arg, True, value(d)) for a, d in zip(f.args.kwonlyargs, f.args.kw_defaults)]

    @classmethod
    def binding_params(cls, function):
        """The same of a pybind11 binding, read from its signature line."""
        line = function.__doc__.splitlines()[0]
        inner, parts, depth, part = line[line.index("(") + 1:line.rindex(") ->")], [], 0, ""
        for ch in inner + ",":
            depth += (ch in "[(") - (ch in "])")
            if ch == "," and depth == 0:
                parts.append(part.strip())
                part = ""
            else:
                part += ch
        out, keyword_only = [], False
        for p in parts:
            if p == "/":
                out.append(("/", False, cls.NONE_GIVEN))
            elif p == "*":
                keyword_only = True
            elif p.startswith("*") and not p.startswith("**"):   # *args: what follows is keyword-only too
                out.append(("*args", False, cls.NONE_GIVEN))
                keyword_only = True
            elif p and not p.startswith("self"):
                out.append((p.split(":")[0], keyword_only, ast.literal_eval(p.split(" = ", 1)[1]) if " = " in p else cls.NONE_GIVEN))
        return out

    @staticmethod
    def binding_doc(obj):
        """A binding's docstring without the signature line pybind11 puts first; a property's as it is."""
        doc = (obj.__doc__ or "").strip()
        if isinstance(obj, property):
            return doc
        head, _, body = doc.partition("\n\n")
        return body.strip() if "->" in head and "(" in head else doc

    @staticmethod
    def same_text(a, b):
        """Equal but for how the lines are broken."""
        return " ".join((a or "").split()) == " ".join((b or "").split())
