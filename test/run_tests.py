import glob
import os
import shutil
import subprocess
from typing import LiteralString, Any

import yaml

BASE_DIR = os.path.dirname(__file__)

TEMPLATE = """
PLATFORM_INCLUDE=/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk/usr/include
COMMON_INCLUDE = {common_includes}
CROSS_COMPILE =
AS = gcc
CC = gcc
LD = gcc

UNITY = {unity_dir}
UNITY_SOURCE = $(UNITY)/src/unity.c
UNITY_OBJECT = {unity_object}
# Compiler flags
CFLAGS = -c -O2 -Wall -Wextra -Wno-unused-parameter -Wno-unused-variable -mno-outline-atomics -I{kernel_dir} -I{common_dir} -I$(UNITY)/src -I$(PLATFORM_INCLUDE) -I$(COMMON_INCLUDE)

# Assembler flags
ASFLAGS = -c -O2 -Wall -Wextra -Wno-unused-parameter -Wno-unused-variable -nostdlib -latomic -I{stubs_dir} -I{fixtures_dir}

# Linker flags
LDFLAGS =

# Output files
RUNNER_BINS := {runner_bins}

C_SOURCES := {c_sources}
ASM_SOURCES := {asm_sources}

KERNEL_SOURCES := {kernel_sources}
KERNEL_ASM_SOURCES := {kernel_asm_sources}

# Define object files based on source files, potentially in a build directory
C_OBJECTS := {c_objects}
ASM_OBJECTS := {asm_objects}

KERNEL_OBJECTS := {kernel_objects}
KERNEL_ASM_OBJECTS := {kernel_asm_objects}

all: $(BIN)

# Semihosting version (better for QEMU debugging)
setup:
	mkdir -p build

{test_base}/build/%.o: {test_base}/%.c
	mkdir -p `dirname $@`
	$(CC) $(CFLAGS) -c $< -o $@

{test_base}/build/%.o: {test_base}/%.s
	mkdir -p `dirname $@`
	$(AS) $(ASFLAGS) $< -o $@

{test_base}/build/%.o: {project_base}/%.c
	mkdir -p `dirname $@`
	$(CC) $(CFLAGS) -c $< -o $@

{test_base}/build/%.o: {project_base}/%.s
	mkdir -p `dirname $@`
	$(AS) $(ASFLAGS) $< -o $@

$(UNITY_OBJECT): $(UNITY_SOURCE)
	mkdir -p `dirname $@`
	$(CC) $(CFLAGS) -c $< -o $@

{runner_tasks}

# Clean build artifacts
clean:
	rm -rf {build_dir}

compile: $(RUNNER_BINS)


.PHONY: all clean qemu
"""

UNITY_PATH = os.environ.get("UNITY_PATH", "vendor/Unity")

def join(files):
    return " ".join(files)

def main():
    shutil.rmtree("build", ignore_errors=True)
    manifests = glob.glob("**/manifest.yml", recursive=True)
    failed_tests = []

    for manifest in manifests:
        test_dir = os.path.dirname(manifest)
        unity_path = os.path.abspath(os.path.join(BASE_DIR, "..", UNITY_PATH))

        test_runners = generate_test_runners(test_dir, unity_path)
        generate_makefile(manifest, test_dir, unity_path, test_runners)
        compile = subprocess.run(["make", "-f", "Makefile.gen", "clean", "compile"], cwd=test_dir, capture_output=True)
        if compile.returncode != 0:
            print(compile.stderr.decode("utf-8"))
            print(compile.stdout.decode("utf-8"))
            print("Compilation failed")
            exit(255)
        for runner in test_runners:
            task_name = f"run-{os.path.basename(runner).replace("_Runner.c", "")}"
            run_tests = subprocess.run(["make", "-f", "Makefile.gen", task_name], cwd=test_dir, capture_output=True)
            print(run_tests.stderr.decode("utf-8"))
            print(run_tests.stdout.decode("utf-8"))
            if run_tests.returncode != 0:
                failed_tests.append(test_dir)
                print("Tests failed")
    if len(failed_tests) > 0:
        print(f"Failed tests: {"\n".join(failed_tests)}")
        exit(1)
    else:
        print(f"All tests passed")


def generate_makefile(manifest: str, test_dir: str | bytes | LiteralString | Any, unity_path: str, test_runners: list[str]):
    test_dir = os.path.abspath(test_dir)
    project_root = os.path.abspath(os.path.join(BASE_DIR, ".."))
    test_dir_rel = os.path.relpath(test_dir, project_root)

    levels = test_dir_rel.split(os.sep)
    steps_back = [".."] * len(levels)
    relative_base = os.path.join(*steps_back)

    # making it relative to the test directory
    project_dir = relative_base
    kernel_dir = os.path.join(project_dir, "kernel")
    common_dir = os.path.join(project_dir, "common")
    common_includes = os.path.join(project_dir, "common_includes")
    unity_dir = os.path.join(project_dir, "vendor", "Unity")
    build_dir = os.path.join(relative_base, "build", test_dir_rel)
    kernel_obj_build_dir = os.path.join(relative_base, "build")
    unity_object = os.path.join(kernel_obj_build_dir, "vendor", "Unity", "src", "unity.o")

    with open(manifest, "r") as manifest_file:
        manifest = yaml.load(manifest_file, Loader=yaml.FullLoader)
        source_files = manifest["manifest"]["source_files"]
        stubs_dir_fs = os.path.join(BASE_DIR, "kernel", "stubs")
        fixtures_dir_fs = os.path.join(BASE_DIR, "kernel", "fixtures")
        stubs_dir = os.path.join("test", "kernel", "stubs")
        fixtures_dir = os.path.join("test", "kernel", "fixtures")
        stubs_dir_relative = os.path.join(relative_base, stubs_dir)
        fixtures_dir_relative = os.path.join(relative_base, fixtures_dir)

        source_expanded = [
            os.path.relpath(expand, test_dir)
            for file in source_files
            for expand in glob.glob(os.path.join(project_root, file))
        ]
        source_asm_files = [file
                            for file
                            in source_expanded
                            if file.endswith(".s")]
        include_default_stubs = manifest["manifest"].get("include_default_stubs", True)
        include_default_fixtures = manifest["manifest"].get("include_default_fixtures", True)
        stubs_c_files = [
            os.path.relpath(expand, test_dir)
            for expand in glob.glob(os.path.join(stubs_dir_fs, "*.c"))
        ] if include_default_stubs else []
        stubs_asm_files = [
            os.path.relpath(expand, test_dir)
            for expand in glob.glob(os.path.join(stubs_dir_fs, "*.s"))
        ] if include_default_stubs else []
        fixtures_c_files = [
            os.path.relpath(expand, test_dir)
            for expand in glob.glob(os.path.join(fixtures_dir_fs, "*.c"))
        ] if include_default_fixtures else []
        fixtures_asm_files = [
            os.path.relpath(expand, test_dir)
            for expand in glob.glob(os.path.join(fixtures_dir_fs, "*.s"))
        ] if include_default_fixtures else []

        source_c_files = [file
                          for file
                          in source_expanded
                          if file.endswith(".c")]
        common_c_files = [
            os.path.relpath(expand, test_dir)
            for expand in glob.glob(os.path.join(project_root, "common/**/*.c"), recursive=True)
        ]

        asm_objects = [os.path.join(kernel_obj_build_dir, file.replace(".s", ".o").replace("../", ""))
                       for file
                       in source_asm_files]

        c_objects = [os.path.join(kernel_obj_build_dir, file.replace(".c", ".o").replace("../", ""))
                     for file
                     in source_c_files]
        common_c_objects = [os.path.join(kernel_obj_build_dir, file.replace(".c", ".o").replace("../", ""))
                            for file
                            in common_c_files]

        test_dir_c_files = [os.path.basename(file) for file in glob.glob(os.path.join(test_dir, "*.c"))]
        test_dir_asm_files = [os.path.basename(file) for file in glob.glob(os.path.join(test_dir, "*.s"))]

        stubs_dir_c_objects = [os.path.join(build_dir, file.replace(".c", ".o"))
                               for file
                               in stubs_c_files]
        stubs_dir_asm_objects = [os.path.join(build_dir, file.replace(".s", ".o"))
                                 for file
                                 in stubs_asm_files]

        fixtures_dir_c_objects = [os.path.join(build_dir, file.replace(".c", ".o"))
                                  for file
                                  in fixtures_c_files]
        fixtures_dir_asm_objects = [os.path.join(build_dir, file.replace(".s", ".o"))
                                    for file
                                    in fixtures_asm_files]

        test_dep_c_objects = [os.path.join(build_dir, file.replace(".c", ".o"))
                              for file
                              in test_dir_c_files
                              if not os.path.basename(file).startswith("test_")]

        test_dep_asm_objects = [os.path.join(build_dir, file.replace(".s", ".o"))
                                for file
                                in test_dir_asm_files]

        with open(os.path.join(test_dir, "Makefile.gen"), "w", encoding="utf-8") as makefile:
            runner_tasks =[_task_template(build_dir, runner) for runner in test_runners]
            runner_bins = [_runner_bin(build_dir, runner) for runner in test_runners]

            makefile.write(TEMPLATE.format(c_sources=join(test_dir_c_files),
                                           asm_sources=join(test_dir_asm_files),
                                           kernel_sources=join([*source_c_files,
                                                                *common_c_files,
                                                                *stubs_c_files,
                                                                *fixtures_c_files]),
                                           kernel_asm_sources=join([*source_asm_files,
                                                                    *stubs_asm_files,
                                                                    *fixtures_asm_files]),
                                           c_objects=join(test_dep_c_objects),
                                           asm_objects=join(test_dep_asm_objects),
                                           kernel_objects=join([*c_objects,
                                                                *common_c_objects,
                                                                *stubs_dir_c_objects,
                                                                *fixtures_dir_c_objects]),
                                           kernel_asm_objects=join([*asm_objects,
                                                                    *stubs_dir_asm_objects,
                                                                    *fixtures_dir_asm_objects]),
                                           unity_dir=unity_dir,
                                           test=os.path.basename(test_dir),
                                           kernel_dir=kernel_dir,
                                           common_dir=common_dir,
                                           project_base=project_dir,
                                           build_dir=build_dir,
                                           test_base=relative_base,
                                           stubs_dir=stubs_dir_relative,
                                           fixtures_dir=fixtures_dir_relative,
                                           common_includes=common_includes,
                                           unity_object=unity_object,
                                           runner_bins=join(runner_bins),
                                           runner_tasks="\n".join(runner_tasks)))

def _runner_bin(build_dir, runner):
    return os.path.join(build_dir, os.path.basename(runner).replace(".c", ".bin"))

def _task_template(build_dir, runner):
    runner_obj = os.path.join(build_dir, os.path.basename(runner).replace(".c", ".o"))
    runner_bin = _runner_bin(build_dir, runner)
    test_obj = os.path.join(build_dir, os.path.basename(runner).replace("_Runner.c", ".o"))
    test_name = os.path.basename(runner).replace("_Runner.c", "")
    return f"""

{runner_bin}: $(UNITY_OBJECT) $(ASM_OBJECTS) $(C_OBJECTS) $(KERNEL_ASM_OBJECTS) $(KERNEL_OBJECTS) {runner_obj} {test_obj}
	$(LD) $(LDFLAGS) $^ -o $@

run-{test_name}: {runner_bin} 
	{runner_bin}
"""

def generate_test_runners(test_dir: str | bytes | LiteralString | Any, unity_path: str):
    tests = [test for test in glob.glob(os.path.join(test_dir, "test_*.c")) if not test.endswith("_Runner.c")]

    return [_generate_runner(test, unity_path) for test in tests]

def _generate_runner(test, unity_path):
    result = subprocess.run(["ruby",
                             f"{unity_path}/auto/generate_test_runner.rb",
                             test], capture_output=True)
    print(result.stdout.decode("utf-8"))
    print(result.stderr.decode("utf-8"))

    return test.replace(".c", "_Runner.c")

if __name__ == "__main__":
    main()
