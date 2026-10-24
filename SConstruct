#!/usr/bin/env python
# Build script for godot-horde. Based on godotengine/godot-cpp-template.
#
#   scons                         Build the GDExtension (template_debug, host platform).
#   scons target=template_release Build an optimized release library.
#   scons tests                   Build and run the pure C++ core unit tests (doctest).
#   scons tests sanitize=yes      Same, with AddressSanitizer + UndefinedBehaviorSanitizer.
import os
import sys

from methods import print_error

libname = "horde"
projectdir = "project"
addon_bin_dir = projectdir + "/addons/horde/bin"

# Lowest Godot version the extension targets. See CLAUDE.md ("Minimum Godot version").
api_version = "4.4"

# Flags that keep the simulation reproducible across compilers (no FMA contraction).
deterministic_fp_flags = ["-ffp-contract=off"]

customs = [os.path.abspath("custom.py")]


def build_core_tests():
    """Builds the doctest suite for src/core. Does not need godot-cpp."""
    opts = Variables(customs, ARGUMENTS)
    opts.Add(BoolVariable("sanitize", "Build tests with ASan + UBSan", False))
    opts.Add(BoolVariable("werror", "Treat warnings as errors", True))
    env = Environment(tools=["default"], ENV=os.environ, variables=opts)
    Help(opts.GenerateHelpText(env))

    if env.get("CC") == "cl":
        print_error("The core test suite currently supports GCC and Clang only.")
        Exit(1)

    env.Append(CXXFLAGS=["-std=c++17"])
    env.Append(CCFLAGS=["-O2", "-g", "-Wall", "-Wextra", "-Wpedantic", "-Wshadow"] + deterministic_fp_flags)
    if env["werror"]:
        env.Append(CCFLAGS=["-Werror"])
    env.Append(CPPPATH=["src/"])
    # doctest is third-party: include it as a system header so its warnings are not ours.
    env.Append(CCFLAGS=["-isystem", Dir("thirdparty/doctest").abspath])
    if env["sanitize"]:
        san = ["-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-fno-sanitize-recover=all"]
        env.Append(CCFLAGS=san, LINKFLAGS=san)

    build_dir = "build/tests" + ("_san" if env["sanitize"] else "")
    env.VariantDir(build_dir, ".", duplicate=0)
    sources = [build_dir + "/" + str(f) for f in Glob("src/core/*.cpp", strings=True)]
    sources += [build_dir + "/" + str(f) for f in Glob("tests/core/*.cpp", strings=True)]
    program = env.Program("bin/tests/horde_core_tests" + ("_san" if env["sanitize"] else ""), sources)

    run = env.Alias("tests", [program], program[0].abspath)
    env.AlwaysBuild(run)
    Default(run)


if "tests" in COMMAND_LINE_TARGETS:
    build_core_tests()
    Return()

localEnv = Environment(tools=["default"], PLATFORM="")

opts = Variables(customs, ARGUMENTS)
opts.Update(localEnv)

Help(opts.GenerateHelpText(localEnv))

env = localEnv.Clone()

if not (os.path.isdir("godot-cpp") and os.listdir("godot-cpp")):
    print_error("""godot-cpp is not available within this folder, as Git submodules haven't been initialized.
Run the following command to download godot-cpp:

    git submodule update --init --recursive""")
    sys.exit(1)

# godot-cpp writes its generated bindings to a single folder (godot-cpp/gen) and bakes
# the pointer size into them, but does not regenerate when switching between 32-bit
# (web, *32 arches) and 64-bit targets. A stale folder compiles fine and crashes at
# runtime, so remember the bitness of the last build and force a regeneration.
bindings_bits_marker = "godot-cpp/gen/.horde_bindings_bits"
target_bits = "32" if ARGUMENTS.get("platform") == "web" or "32" in ARGUMENTS.get("arch", "") else "64"
previous_bits = open(bindings_bits_marker).read().strip() if os.path.isfile(bindings_bits_marker) else ""
if previous_bits != target_bits:
    env["generate_bindings"] = True

env = SConscript("godot-cpp/SConstruct", {"api_version": api_version, "env": env, "customs": customs})

env.Append(CPPPATH=["src/"])
# Keep our object files apart per platform/target/arch, like godot-cpp does for its own.
env["SHOBJSUFFIX"] = env["suffix"] + env["SHOBJSUFFIX"]

# The core is engine-agnostic C++; compile it with deterministic floating point flags.
core_env = env.Clone()
if not env.get("is_msvc", False):
    core_env.Append(CCFLAGS=deterministic_fp_flags)
sources = [core_env.SharedObject(f) for f in Glob("src/core/*.cpp")]
sources += Glob("src/godot/*.cpp")

if env["target"] in ["editor", "template_debug"]:
    try:
        doc_data = env.GodotCPPDocData("src/gen/doc_data.gen.cpp", source=Glob("doc_classes/*.xml"))
        sources.append(doc_data)
    except AttributeError:
        print("Not including class reference as we're targeting a pre-4.3 baseline.")

# .dev doesn't inhibit compatibility, so we don't need to key it.
# .universal just means "compatible with all relevant arches" so we don't need to key it.
suffix = env["suffix"].replace(".dev", "").replace(".universal", "")

lib_filename = "{}{}{}{}".format(env.subst("$SHLIBPREFIX"), libname, suffix, env.subst("$SHLIBSUFFIX"))

library = env.SharedLibrary(
    "bin/{}/{}".format(env["platform"], lib_filename),
    source=sources,
)

copy = env.Install("{}/{}/".format(addon_bin_dir, env["platform"]), library)


def write_bindings_bits(target, source, env):
    with open(bindings_bits_marker, "w") as f:
        f.write(target_bits + "\n")


env.AddPostAction(library, write_bindings_bits)

default_args = [library, copy]
Default(*default_args)
