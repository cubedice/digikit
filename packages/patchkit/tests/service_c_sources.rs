//! A Rust service object that also lists ColdFire C and assembler sources after its `Cargo.toml` (`packages/patchkit/src/build/rust.rs`): the
//! fixture `tests/data/services/svc-test` (its README) through build, pack, service-check and bind for the DT2, and the checks of the link.
//! The tests skip without the m68k-elf toolchain or the Rust-for-ColdFire toolchain; bind also skips without the stock DT2 image.

use dkneeds::keys::{Keys, name_hash};
use patchkit::build::toolchain::{Env, TempDir};
use patchkit::build::{self, BuildOptions, rust};
use patchkit::dkm::Dkm;
use patchkit::dko::Dko;
use std::path::{Path, PathBuf};
use std::process::Command;

fn root() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR"))
        .join("../..")
        .canonicalize()
        .unwrap()
}

fn fixture() -> PathBuf {
    root().join("packages/patchkit/tests/data/services/svc-test")
}

/// The toolchains a Rust object needs: the m68k-elf tools and the Rust-for-ColdFire back end.
fn toolchains() -> Option<Env> {
    let env = Env::new(&root());
    if !env.m68k_or_skip() {
        return None;
    }
    if !env.codegen_gcc_ready() {
        eprintln!("SKIP: the Rust-for-ColdFire toolchain is not installed");
        return None;
    }
    Some(env)
}

fn patchkit(args: &[&str]) -> std::process::Output {
    Command::new(env!("CARGO_BIN_EXE_patchkit"))
        .arg("--root")
        .arg(root())
        .args(args)
        .current_dir(root())
        .output()
        .unwrap()
}

fn ok(what: &str, o: &std::process::Output) {
    assert!(
        o.status.success(),
        "{what} failed:\n{}{}",
        String::from_utf8_lossy(&o.stdout),
        String::from_utf8_lossy(&o.stderr)
    );
}

#[test]
fn a_service_object_with_c_and_assembler_sources_builds_packs_checks_and_binds() {
    let Some(_) = toolchains() else { return };
    let dks = fixture().join("dks");
    let t = TempDir::new("pk-svc-c-").unwrap();

    // build: the crate, hook.c (with DK_SERVICE) and exports.s in one object; what the crate's exports reach stays
    let r = build::build_module(
        &dks,
        &BuildOptions {
            root: root(),
            force: true,
            ..Default::default()
        },
    )
    .unwrap();
    assert_eq!(r.objects.len(), 1);
    assert_eq!(r.objects[0].status, build::Status::Built);
    let object = Dko::parse(&std::fs::read_to_string(&r.objects[0].path).unwrap()).unwrap();
    object.require_isa_checked().unwrap();
    let defined = |n: &str| {
        object
            .symbols
            .iter()
            .any(|s| s.name == n && s.section.is_some() && s.global)
    };
    for n in [
        "dk_module",
        "dk_svc_abi",
        "svc_test_settings_tail",
        "dk_svctest_hits",
        "dk_svctest_ping",
    ] {
        assert!(defined(n), "{n} is not a global of the object");
    }
    assert!(
        !object.symbols.iter().any(|s| s.name == "svc_test_unused"),
        "a C function no export reaches is dropped"
    );
    assert!(object.relocs.iter().all(|r| r.rtype == "cf.32"));
    assert!(object.toolchain["cflags"].ends_with("-DDK_SERVICE=1"));
    assert_eq!(object.toolchain["exports"], "dk_module dk_svc_abi");
    // the stored object is current with its sources (`build --check` would pass)
    assert_eq!(
        build::module_status(&root(), &dks).unwrap(),
        [("build/svc-test.dko.json".to_string(), true)]
    );

    // pack: a .dks that provides svc-test, exports both functions and takes the C hook
    let manifest = dks.join("module.toml");
    let out = t.path().join("svc-test.dks");
    let map = t.path().join("svc-test.addrmap.json");
    ok(
        "pack",
        &patchkit(&[
            "pack",
            "core/services/svc-test",
            "--manifest",
            manifest.to_str().unwrap(),
            "--device",
            "dt2",
            "--addrmap",
            map.to_str().unwrap(),
            "-o",
            out.to_str().unwrap(),
        ]),
    );
    let bytes = std::fs::read(&out).unwrap();
    let dkm = Dkm::parse(&bytes).unwrap();
    assert_eq!(dkm.role, 1);
    let k = Keys::parse(&dkm.register).unwrap();
    assert_eq!(
        k.provides().collect::<Vec<_>>(),
        vec![(name_hash("svc-test"), dkm.version)]
    );
    let mut exports: Vec<u32> = k.exports().map(|e| e.a).collect();
    exports.sort();
    let mut want = vec![name_hash("dk_svctest_ping"), name_hash("dk_svctest_hits")];
    want.sort();
    assert_eq!(exports, want);
    assert!(k.exports().all(|e| (e.b as usize) < dkm.image.len()));
    let hooks: Vec<_> = k.hooks().collect();
    assert_eq!(hooks.len(), 1);
    assert_eq!(hooks[0].a, name_hash("cf.menu.hook.settings_list.tail"));
    assert!((hooks[0].b as usize) < dkm.image.len());

    // service-check: the layout of a service folder, the hook a routine of a listed C source
    let sdk = root().join("sdk/sdk.toml");
    let check = patchkit(&[
        "service-check",
        fixture().to_str().unwrap(),
        "--sdk",
        sdk.to_str().unwrap(),
    ]);
    ok("service-check", &check);
    assert!(String::from_utf8_lossy(&check.stdout).contains(": ok"));

    // bind to the stock DT2 firmware: the trampoline of the C hook is appended
    let firmware = root().join("firmware/Digitakt_II_OS1.17.syx");
    if !firmware.is_file() {
        eprintln!("SKIP bind: {} is not here", firmware.display());
        return;
    }
    let bound = t.path().join("bound.dks");
    ok(
        "bind",
        &patchkit(&[
            "bind",
            out.to_str().unwrap(),
            "--firmware",
            firmware.to_str().unwrap(),
            "-o",
            bound.to_str().unwrap(),
            "--addrmap",
            map.to_str().unwrap(),
        ]),
    );
    let bound_bytes = std::fs::read(&bound).unwrap();
    assert!(
        bound_bytes.len() > bytes.len(),
        "bind appends the trampoline"
    );
    Dkm::parse(&bound_bytes).unwrap();
    assert!(bound.with_extension("src.json").is_file());
}

/// A crate in a scratch directory with `#[no_mangle]` entries that call C and assembler routines.
fn scratch_crate(dir: &Path) -> PathBuf {
    let write = |rel: &str, text: &str| {
        std::fs::create_dir_all(dir.join(rel).parent().unwrap()).unwrap();
        std::fs::write(dir.join(rel), text).unwrap();
    };
    write(
        "Cargo.toml",
        "[package]\nname = \"cf-c-sources-proof\"\nversion = \"0.1.0\"\nedition = \"2021\"\n[lib]\ncrate-type = [\"staticlib\"]\n[workspace]\n[profile.release]\npanic = \"abort\"\nopt-level = \"s\"\n",
    );
    write(
        "src/lib.rs",
        "#![no_std]\n#[panic_handler]\nfn panic(_: &core::panic::PanicInfo) -> ! { loop {} }\nextern \"C\" {\n    fn c_div(a: u64, b: u64) -> u64;\n    fn asm_far();\n}\n#[no_mangle]\npub extern \"C\" fn rs_div(a: u64, b: u64) -> u64 { unsafe { c_div(a, b) } }\n#[no_mangle]\npub extern \"C\" fn rs_far() { unsafe { asm_far() } }\n",
    );
    // a 64-bit division: gcc calls __udivdi3, which must come from compiler_builtins
    write(
        "c/div.c",
        "#include <stdint.h>\nuint64_t c_div(uint64_t a, uint64_t b);\nuint64_t c_div(uint64_t a, uint64_t b) { return a / b; }\n",
    );
    // a PC-relative call to a symbol outside the section
    write(
        "c/far.s",
        "    .text\n    .globl asm_far\nasm_far:\n    bsr.l far_away\n    rts\n",
    );
    dir.join("Cargo.toml")
}

#[test]
fn the_link_takes_helpers_from_compiler_builtins_and_names_the_source_of_a_pc_relative_reference() {
    let Some(env) = toolchains() else { return };
    let d = TempDir::new("pk-svc-c-link-").unwrap();
    let manifest = scratch_crate(d.path());
    let c = |files: &[&str]| rust::CSources {
        files: files.iter().map(|f| d.path().join(f)).collect(),
        include_dirs: vec![],
    };
    let (proto, hash, _) = rust::object_proto_with(&env, &manifest, &[], &c(&["c/div.c"])).unwrap();
    assert_eq!(
        hash,
        rust::object_sha256(&env, &manifest, &[], &c(&["c/div.c"])).unwrap()
    );
    let helper = proto
        .symbols
        .iter()
        .find(|s| s.name == "__udivdi3" && s.section.is_some())
        .expect("__udivdi3 from compiler_builtins");
    assert!(!helper.global, "compiler helpers are private to the object");
    assert!(
        proto
            .symbols
            .iter()
            .any(|s| s.name == "c_div" && s.section.is_some())
    );
    let e = rust::object_proto_with(&env, &manifest, &[], &c(&["c/div.c", "c/far.s"])).unwrap_err();
    assert!(e.0.contains("c/far.s"), "{e}");
    assert!(
        e.0.contains("R_68K_PC32") || e.0.contains("R_68K_PLT32"),
        "{e}"
    );
}
