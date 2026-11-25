fn main() {
    println!("cargo:rerun-if-changed=../src/cf/ownership.c");
    println!("cargo:rerun-if-changed=../src/cf/ownership.h");
    println!("cargo:rerun-if-changed=tests/ownership.c");
    println!("cargo:rerun-if-changed=tests/dispatch.c");
    println!("cargo:rerun-if-changed=tests/transport.c");
    println!("cargo:rerun-if-changed=../src/cf/transport.c");
    println!("cargo:rerun-if-changed=../src/cf/transport.h");
    println!("cargo:rerun-if-changed=../src/cf/adapter.c");
    println!("cargo:rerun-if-changed=../src/cf/image.c");
    println!("cargo:rerun-if-changed=../src/cf/bridge.h");
    println!("cargo:rerun-if-changed=../../../../runtime/platform/platform/src/cf/midi_dispatch.c");
    // image.c is the image part of the module: the channel router `mi_channel_message` (USB and DIN call it through the weak
    // reference of midi_dispatch.c), which hands a message to adapter.c's `mi_legacy_channel_message`. Without the track
    // keyboard on the host its weak `tk_midi_channel_message` is null, so the router is the ledger alone. Nothing refers to
    // the router strongly, so the archive is linked whole: a weak reference alone pulls no member out of it (in the image
    // the router is always there).
    cc::Build::new()
        .link_lib_modifier("+whole-archive")
        .std("c11")
        .warnings(true)
        .flag("-Wall")
        .flag("-Wextra")
        .flag("-Werror")
        .include("../src/cf")
        .file("../src/cf/ownership.c")
        .file("tests/ownership.c")
        .file("tests/dispatch.c")
        .file("tests/transport.c")
        .file("../src/cf/transport.c")
        .file("../src/cf/adapter.c")
        .file("../src/cf/image.c")
        .file("../../../../runtime/platform/platform/src/cf/midi_dispatch.c")
        .compile("dkmidi_ownership");
}
