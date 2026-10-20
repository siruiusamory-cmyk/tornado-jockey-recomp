import os
import pathlib
import re
import subprocess
import unittest

ROOT = pathlib.Path(__file__).resolve().parent.parent
BUILD = ROOT / "build" / "proxy-test"
GENERATED = BUILD / "legacy_rules.h"
GREEN_EXE = BUILD / "fixed_function_test.exe"
RED_EXE = BUILD / "fixed_function_test_red.exe"


FIXTURE = ROOT / "tools" / "fixtures" / "legacy-rules.txt"
COUNT_PATTERNS = [
    ("index block", re.compile(r"UINT\* expanded = new UINT\[([^\]]+)\];")),
    ("emit count", re.compile(r"emit\(base, expanded, ([^,]+), firstIndex\);")),
    ("draw count", re.compile(r"DrawInstanced\(([^,]+), 1, 0, 0\);")),
]
SRV_PATTERNS = [
    ("slot assignment", re.compile(r"t->srv = device->nextSrv;")),
    ("slot step", re.compile(r"device->nextSrv = device->nextSrv >")),
]
SRV_CONSTANTS = [
    ("heap size", re.compile(r"static constexpr unsigned kDescriptors = \d+;")),
    ("ring limit", re.compile(r"static constexpr unsigned kRingLimit = \d+;")),
    ("first slot", re.compile(r"unsigned nextSrv = kDescriptors - 1;")),
]


QUOTES = [
    "    if (rs[kRsAlphaTest] && (rs[kRsAlphaFunc] == 7 || rs[kRsAlphaFunc] == 5)) {",
    "    if (rs[kRsFogEnable] && (rs[kRsFogTableMode] || rs[kRsFogVertexMode])) params.flags |= 8;",
    "        rs[kRsBlendOp] == 2 || rs[kRsBlendOp] == 3 ? d.BlendState.RenderTarget[0].BlendOp",
    "    if (op0 != 1) c = stage_apply(op0, arg00, arg01, aop0, aarg00, aarg01, c, i.color, t0, factor,",
    "    if (op1 != 1) c = stage_apply(op1, arg10, arg11, aop1, aarg10, aarg11, c, i.color, t1, factor,",
    "    r.fence = fenceValue + 1;",
]


CPP_RULES = ROOT / "port" / "proxy" / "render" / "fixed_function.h"
SHADER = ROOT / "port" / "proxy" / "render" / "shaders" / "tj.hlsl"
CPP_CASE = re.compile(r"case (\d+): return ([^;]+);")
CPP_DEFAULT = re.compile(r"default: return ([^;]+);")
HLSL_BRANCH = re.compile(r"(?:if|else if) \(alphaFunc == (\d+)\) keep = ([^;]+);")
HLSL_DEFAULT = re.compile(r"else keep = ([^;]+);")


def canonical(expr):
    text = re.sub(r"\s+", " ", expr.strip().lower())
    for name in ("alpha8", "a8"):
        text = re.sub(rf"\b{name}\b", "a", text)
    for name in ("ref8", "r8"):
        text = re.sub(rf"\b{name}\b", "r", text)
    return text


def alpha_pass_body(cpp_text):
    start = cpp_text.index("inline bool alpha_pass(")
    start = cpp_text.index("{", start)
    depth = 0
    for i in range(start, len(cpp_text)):
        if cpp_text[i] == "{":
            depth += 1
        elif cpp_text[i] == "}":
            depth -= 1
            if depth == 0:
                return cpp_text[start : i + 1]
    raise AssertionError("alpha_pass has no closing brace")


def alpha_table_mismatches(cpp_text, shader_text):
    body = alpha_pass_body(cpp_text)
    cpp = {int(m.group(1)): canonical(m.group(2)) for m in CPP_CASE.finditer(body)}
    default = CPP_DEFAULT.search(body)
    shader = {int(m.group(1)): canonical(m.group(2)) for m in HLSL_BRANCH.finditer(shader_text)}
    shader_default = HLSL_DEFAULT.search(shader_text)
    if default is None or shader_default is None:
        raise AssertionError("no default branch in the alpha comparison table and the shader")
    differing = sorted(f for f in set(cpp) | set(shader) if cpp.get(f) != shader.get(f))
    return differing, (canonical(default.group(1)), canonical(shader_default.group(1)))


TABLE_MUTATIONS = (
    "retireQueue.push(",
    "retireQueue.push_fenced(",
    "retireQueue.bind(",
    "retireQueue.drain(",
    "retireQueue.drain_pending(",
    "srvSlots.alloc(",
    "srvSlots.release(",
)


def device_functions(text):
    found = []
    for match in re.finditer(r"^inline [^\n]*?Device12::(\w+)\(", text, re.MULTILINE):
        opening = text.index("{", match.end())
        depth = 0
        for i in range(opening, len(text)):
            if text[i] == "{":
                depth += 1
            elif text[i] == "}":
                depth -= 1
                if depth == 0:
                    found.append((match.group(1), text[opening : i + 1]))
                    break
        else:
            raise AssertionError(f"{match.group(1)} has no closing brace")
    return found


def unguarded_mutations(impl1):
    return [
        (name, mutation)
        for name, body in device_functions(impl1)
        for mutation in TABLE_MUTATIONS
        if mutation in body and "own_thread(" not in body
    ]


RED_FAILURES = [
    "a 4-vertex strip is two triangles: 4 indices, want 6",
    "a block too small for the expansion is refused",
    "a 4-index block is refused for a 4-vertex strip",
    "a refused expansion writes nothing",
    "the pre-repair expansion wrote past its block",
    "D3DCMP_NEVER discards even at alpha 255",
    "a disabled stage 0 stops an enabled stage 1",
    "D3DFOG_EXP is not fogged as linear",
    "D3DFOG_EXP table fog is not applied as a linear fog",
    "D3DFOG_EXP2 vertex fog is not applied as a linear fog",
    "EXP table fog wins over a linear vertex mode",
    "D3DBLENDOP_MIN reaches the alpha channel as MIN",
    "8,192 live textures hold 8,192 distinct descriptors",
    "an exhausted allocator returns the white texel",
    "the released slot is the next one handed out",
    "a release during recording waits for the list",
    "an upload fence does not release what the open list reads",
    "the list's own fence frees the resource and its descriptor slot",
    "the pending bucket holds its bound",
    "a failed wait keeps the bucket with no transfer in flight",
    "a completed wait clears the transfer flag and releases the bucket",
    "a completed wait releases the pending resource and its slot",
    "a second thread is refused",
    "a refusal fails the frame",
    "every refusal is counted",
    "one index past a 16-bit buffer is refused",
    "one index past a 32-bit buffer is refused",
    "an empty index buffer overruns",
    "an over-long index count is clamped to the index buffer",
    "a start index at the end of the index buffer is rejected",
    "a start index past the index buffer is rejected",
]


def pinned(name):
    sections = re.split(r"^## ", FIXTURE.read_text(encoding="utf-8"), flags=re.MULTILINE)
    for part in sections[1:]:
        title, _, body = part.partition("\n")
        if title.strip() == name:
            return body
    raise AssertionError(f"the fixture has no section {name!r}")


def one_line(text, label, pattern):
    found = [line for line in text.splitlines() if pattern.search(line)]
    if len(found) != 1:
        raise AssertionError(
            f"pre-repair {label}: {len(found)} matching lines in the fixture, want exactly 1"
        )
    return found[0]


def statement(text, label, pattern):
    lines = text.splitlines()
    starts = [i for i, line in enumerate(lines) if pattern.search(line)]
    if len(starts) != 1:
        raise AssertionError(
            f"pre-repair {label}: {len(starts)} matching lines in the fixture, want exactly 1"
        )
    parts = []
    for line in lines[starts[0] :]:
        parts.append(line.strip())
        if line.rstrip().endswith(";"):
            return " ".join(parts)
    raise AssertionError(f"pre-repair {label}: no statement end in the fixture")


def check_quotes():
    text = pinned("transcribed rules")
    for quote in QUOTES:
        if quote not in text:
            raise AssertionError(
                f"the RED transcription quotes a line the fixture no longer has: {quote!r}"
            )


def brace_block(text, opening, label):
    lines = text.splitlines()
    start = [i for i, line in enumerate(lines) if line == opening]
    if len(start) != 1:
        raise AssertionError(f"{label}: {len(start)} matching lines in the fixture, want exactly 1")
    depth = 0
    for i in range(start[0], len(lines)):
        depth += lines[i].count("{") - lines[i].count("}")
        if depth == 0 and i > start[0]:
            return lines[start[0] : i + 1]
    raise AssertionError(f"{label}: unbalanced block in the fixture")


def write_legacy_rules():
    text = pinned("red draw")
    found = {label: one_line(text, label, pattern) for label, pattern in COUNT_PATTERNS}
    counts = {pattern.search(found[label]).group(1) for label, pattern in COUNT_PATTERNS}
    if len(counts) != 1:
        raise AssertionError(
            f"the pre-repair draw used different index counts per statement: {sorted(counts)}"
        )
    index_expr = counts.pop()

    allocation = one_line(
        text,
        "index block allocation",
        re.compile(r"^    UINT\* expanded = new UINT\[[^\]]+\];$"),
    )
    after = text.split(allocation, 1)[1]
    expansion = brace_block(after, "    if (primitive == 4) {", "expansion block")

    srv = pinned("texture slot allocator")
    assign = statement(srv, "slot assignment", SRV_PATTERNS[0][1])
    step = statement(srv, "slot step", SRV_PATTERNS[1][1])
    header = pinned("texture slot constants")
    constants = [one_line(header, label, pattern).strip() for label, pattern in SRV_CONSTANTS]

    lines = [
        "// Generated by tools/test_fixed_function.py. Do not edit.",
        "// Extraction 1: the pre-repair draw, tools/fixtures/legacy-rules.txt. Its index count, used",
        "// for the index block, the emit count and DrawInstanced. The three statements must agree.",
        "namespace red {",
        "typedef unsigned UINT;  // the extracted statements' own spelling of the index type",
        "static inline unsigned legacy_out_count(unsigned primitive, unsigned vertices,",
        "                                        unsigned triangles) {",
        "    (void)primitive;",
        "    (void)triangles;",
        f"    return {index_expr};",
        "}",
        "",
        "// Extraction 2: the same fixture's expansion statements, verbatim and brace-balanced. They",
        "// wrote 3 * (vertices - 2) indices into the block legacy out_count sized by the vertex",
        "// count, which is the overrun this round's guard detects.",
        "static inline void legacy_expand(unsigned primitive, const unsigned* indexList,",
        "                                 unsigned vertices, unsigned triangles, unsigned* expanded) {",
        *expansion,
        "}",
        "",
        "// Extraction 3: the pre-repair texture-slot allocator, verbatim inside a class with the",
        "// names its statements use.",
        "struct Device12 {",
        f"    {constants[0]}",
        f"    {constants[1]}",
        f"    {constants[2]}",
        "    unsigned reused = 0, exhausted = 0;  // the test reads these; the old allocator had none",
        "    unsigned alloc() {",
        "        struct Slot {",
        "            unsigned srv;",
        "        } slot = {0};",
        "        Slot* t = &slot;",
        "        Device12* device = this;",
        f"        {assign}",
        f"        {step}",
        "        return t->srv;",
        "    }",
        "    void release(unsigned) {}  // the old allocator had no free list",
        "};",
        "}  // namespace red",
    ]
    generated = "\n".join(lines) + "\n"
    if not GENERATED.exists() or GENERATED.read_text(encoding="ascii") != generated:
        GENERATED.write_text(generated, encoding="ascii")


def compiler():
    vswhere = (
        pathlib.Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"))
        / "Microsoft Visual Studio"
        / "Installer"
        / "vswhere.exe"
    )
    if not vswhere.exists():
        raise AssertionError(f"vswhere.exe not found: {vswhere}")
    vs = subprocess.run(
        [str(vswhere), "-latest", "-products", "*", "-property", "installationPath"],
        check=False,
        capture_output=True,
        text=True,
    ).stdout.strip()
    return pathlib.Path(vs) / "VC" / "Auxiliary" / "Build" / "vcvars64.bat"


def compile_exe(exe, defines=""):
    vcvars = compiler()
    BUILD.mkdir(parents=True, exist_ok=True)
    batch = BUILD / f"{exe.stem}.bat"
    batch.write_text(
        f'@call "{vcvars}" >nul 2>&1\n'
        f"@cl /nologo /EHsc /W4 /std:c++17 {defines}"
        f' /I"{BUILD}" "{ROOT / "port" / "proxy" / "fixed_function_test.cpp"}"'
        f' /Fe:"{exe}" /Fo:"{BUILD / (exe.stem + ".obj")}"\n',
        encoding="ascii",
    )
    built = subprocess.run([str(batch)], check=False, capture_output=True, text=True)
    if built.returncode or not exe.exists():
        raise AssertionError(f"{exe.name} did not compile:\n{built.stdout}{built.stderr}")


def build_both():
    sources = [
        ROOT / "port" / "proxy" / "render" / "fixed_function.h",
        ROOT / "port" / "proxy" / "fixed_function_test.cpp",
    ]
    check_quotes()
    write_legacy_rules()
    sources.append(GENERATED)
    if all(
        exe.exists() and all(p.stat().st_mtime <= exe.stat().st_mtime for p in sources)
        for exe in (GREEN_EXE, RED_EXE)
    ):
        return
    compile_exe(GREEN_EXE)
    compile_exe(RED_EXE, defines="/DTJ_RED_HISTORY")


class TestFixedFunction(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        build_both()

    def test_rules_green(self):
        ran = subprocess.run([str(GREEN_EXE)], check=False, capture_output=True, text=True)
        self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
        self.assertIn("0 failures", ran.stdout)

    def test_prepair_rules_fail_every_new_guard(self):
        ran = subprocess.run([str(RED_EXE)], check=False, capture_output=True, text=True)
        with self.subTest(rule="the pre-repair rules are rejected"):
            self.assertNotEqual(ran.returncode, 0, ran.stdout)
        missing = [line for line in RED_FAILURES if line not in ran.stdout]
        self.assertEqual(missing, [], f"these guards passed under the pre-repair rules: {missing}")

        self.assertRegex(ran.stdout, r"pre-repair: \d+ checks, [1-9]\d* failures")

    def test_alpha_table_matches_the_shader(self):
        differing, defaults = alpha_table_mismatches(
            CPP_RULES.read_text(encoding="utf-8"), SHADER.read_text(encoding="utf-8")
        )
        self.assertEqual(differing, [], f"the alpha table and the shader disagree on {differing}")
        self.assertEqual(defaults, ("true", "true"), f"the two defaults differ: {defaults}")

    def test_a_divergent_shader_copy_is_caught(self):

        broken = SHADER.read_text(encoding="utf-8").replace("keep = a8 <= r8", "keep = a8 < r8")
        differing, _ = alpha_table_mismatches(CPP_RULES.read_text(encoding="utf-8"), broken)
        self.assertEqual(differing, [4], "the checker missed a shader branch that disagrees")

    def test_alpha_table_reader_ignores_another_switch(self):

        text = CPP_RULES.read_text(encoding="utf-8")
        decoy = (
            "inline bool decoy(unsigned f) {\n"
            "    switch (f) {\n"
            "        case 4: return false;\n"
            "        default: return false;\n"
            "    }\n"
            "}\n\n"
        )
        injected = text.replace("inline bool alpha_pass(", decoy + "inline bool alpha_pass(", 1)
        differing, defaults = alpha_table_mismatches(injected, SHADER.read_text(encoding="utf-8"))
        self.assertEqual(differing, [], f"another switch changed the alpha table: {differing}")
        self.assertEqual(
            defaults, ("true", "true"), f"another default reached the table: {defaults}"
        )

    def test_submit_sites_bind_their_own_signalled_fence(self):
        impl1 = (ROOT / "port" / "proxy" / "render" / "render_d3d12_impl1.h").read_text(
            encoding="utf-8"
        )
        impl2 = (ROOT / "port" / "proxy" / "render" / "render_d3d12_impl2.h").read_text(
            encoding="utf-8"
        )
        impl3 = (ROOT / "port" / "proxy" / "render" / "render_d3d12_impl3.h").read_text(
            encoding="utf-8"
        )
        functions = dict(device_functions(impl2))
        sites = {name: body for name, body in functions.items() if "submit_pending(" in body}
        self.assertEqual(
            sorted(sites), ["CopyRects", "Present", "flush_draws"], "a submit site is missing"
        )
        wait = dict(device_functions(impl1))["wait_gpu"]
        self.assertLess(
            wait.index("++fenceValue;"),
            wait.index("queue->Signal(fence, fenceValue)"),
            "wait_gpu signals before it advances the fence value",
        )
        self.assertIn("completed >= fenceValue", wait, "wait_gpu does not test its own value")
        for name, body in sites.items():
            self.assertEqual(
                body.count("submit_pending("), 1, f"{name} has more than one submit site"
            )
            self.assertIn(
                "submit_pending(fenceValue + 1)",
                body,
                f"{name} binds a value its own wait does not signal",
            )
            at = body.index("submit_pending(fenceValue + 1)")
            after = body[at + len("submit_pending(fenceValue + 1)") :]
            self.assertRegex(
                after,
                r"^\s*;\s*if \(!wait_gpu\(",
                f"{name} binds a value its own wait does not signal",
            )
        draw = impl3[
            impl3.index("inline HRESULT Device12::draw_triangles") : impl3.index(
                "inline HRESULT Device12::DrawPrimitive"
            )
        ]
        first_bind = draw.index("bind_texture(")
        second_bind = draw.index("bind_texture(", first_bind + 1)
        first_guard = draw.index("if (frameFailed)", first_bind)
        second_guard = draw.index("if (frameFailed)", second_bind)
        self.assertLess(first_bind, first_guard, "the stage-0 bind has no guard")
        self.assertLess(first_guard, second_bind, "the stage-1 bind runs after a failed stage 0")
        self.assertLess(second_bind, second_guard, "the stage-1 bind has no guard")
        fetch = draw.index("IASetVertexBuffers(", second_guard)
        recorded = draw.index("DrawInstanced(", fetch)
        self.assertLess(second_guard, fetch, "a failed bind records the vertex fetch")
        self.assertLess(fetch, recorded, "the vertex fetch is not the draw's own")
        for guard in (first_guard, second_guard):
            self.assertIn("++skipped", draw[guard : guard + 120], "the failed draw is not counted")
            self.assertIn("return E_FAIL", draw[guard : guard + 120], "the guard does not fail")
        bind = impl2[
            impl2.index("inline void Device12::bind_texture") : impl2.index(
                "inline D3D12_BLEND blend12"
            )
        ]
        stopped = bind.index("if (frameFailed) return;")
        self.assertLess(stopped, bind.index("ResourceBarrier("), "a failed bind records a barrier")
        self.assertLess(
            stopped, bind.rindex("CopyDescriptorsSimple("), "a failed bind copies a descriptor"
        )

    def test_every_lifetime_table_mutation_checks_the_owner(self):
        impl1 = (ROOT / "port" / "proxy" / "render" / "render_d3d12_impl1.h").read_text(
            encoding="utf-8"
        )
        self.assertEqual(unguarded_mutations(impl1), [], "a table write skips the owner check")
        guard = dict(device_functions(impl1))["own_thread"]
        self.assertIn("threadGuard.enter(", guard, "the device copies the guard decision")

    def test_shutdown_calls_the_tested_predicate(self):
        impl1 = (ROOT / "port" / "proxy" / "render" / "render_d3d12_impl1.h").read_text(
            encoding="utf-8"
        )
        drain = impl1[
            impl1.index("inline void Device12::drain_retired") : impl1.index(
                "inline void Device12::shutdown"
            )
        ]
        self.assertIn("RetireQueue::release_pending_on_complete(complete, xferPending)", drain)
        self.assertIn("drain_pending(releasePending,", drain)
        self.assertNotIn("drain_pending(!xferPending", drain)
        self.assertNotIn("if (complete) xferPending = false", drain)

    def test_texture_matrix_covers_every_texture_state(self):
        header = (ROOT / "port" / "proxy" / "render" / "render_d3d12.h").read_text(encoding="utf-8")
        impl2 = (ROOT / "port" / "proxy" / "render" / "render_d3d12_impl2.h").read_text(
            encoding="utf-8"
        )
        declared = re.search(r"float textureMatrix\[(\d+)\]\[16\]", header)
        self.assertIsNotNone(declared, "textureMatrix is not declared")
        self.assertIn("textureMatrix[state < 16 ? 0 : state - 16]", impl2)
        self.assertGreaterEqual(
            int(declared.group(1)),
            8,
            "the d3d8 texture-transform states are 16..23, so a shorter table is overwritten",
        )

    def test_fog_follows_eye_distance(self):
        impl3 = (ROOT / "port" / "proxy" / "render" / "render_d3d12_impl3.h").read_text(
            encoding="utf-8"
        )
        shader = SHADER.read_text(encoding="utf-8")
        self.assertIn(
            "o[12] = clip[3] != 0 ? 1.0f / clip[3] : 0.0f;",
            impl3,
            "d3d8 pixel fog ramps over FOGSTART..FOGEND in eye distance; the vertex carries "
            "one over w so the pixel stage can rebuild w",
        )
        self.assertIn("float invW : TEXCOORD2;", shader)
        self.assertIn("noperspective float invW : TEXCOORD2;", shader)
        self.assertIn("float eye = i.invW != 0.0 ? 1.0 / i.invW : 0.0;", shader)
        self.assertNotIn("i.fog", shader)

    def test_mipfilter_none_samples_the_base_level(self):

        impl1 = (ROOT / "port" / "proxy" / "render" / "render_d3d12_impl1.h").read_text(
            encoding="utf-8"
        )
        impl3 = (ROOT / "port" / "proxy" / "render" / "render_d3d12_impl3.h").read_text(
            encoding="utf-8"
        )
        shader = SHADER.read_text(encoding="utf-8")
        self.assertIn("noMip.MaxLOD = 0.0f;", impl1, "MIPFILTER NONE needs a base-level sampler")
        self.assertIn("mip_filter_flag(0, tss[0][18]", impl3, "stage 0 mip filter is not read")
        self.assertIn("mip_filter_flag(1, tss[1][18]", impl3, "stage 1 mip filter is not read")
        self.assertIn("SamplerState samp1 : register(s1);", shader)
        self.assertIn("tex0.Sample(samp1, i.uv)", shader)
        self.assertIn("tex1.Sample(samp1, i.uv1)", shader)

    def test_index_guard_records_the_draw(self):
        impl3 = (ROOT / "port" / "proxy" / "render" / "render_d3d12_impl3.h").read_text(
            encoding="utf-8"
        )
        proxy = (ROOT / "port" / "proxy" / "d3d8_proxy.cpp").read_text(encoding="utf-8")
        self.assertIn(
            "index_overruns(startIndex, vertices, indices->length, indices->format)", impl3
        )
        self.assertIn(
            "clamp_index_count(startIndex, vertices, indices->length, indices->format)", impl3
        )
        self.assertIn("# d3d12 index guard start=%u verts=%u", impl3)
        self.assertIn("expand list=%08X verts=%u start=%u", proxy)
        self.assertIn("index_trace()", impl3)
        self.assertIn("index_trace()", proxy)


if __name__ == "__main__":
    unittest.main()
