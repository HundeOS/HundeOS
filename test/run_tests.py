import glob
import os
import subprocess
from typing import LiteralString, Any

import yaml

BASE_DIR = os.path.dirname(__file__)

TEMPLATE = """
PLATFORM_INCLUDE=/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk/usr/include

CROSS_COMPILE =
AS = gcc
CC = gcc
LD = gcc

UNITY = {unity_path}
# Compiler flags
CFLAGS = -c -O2 -Wall -Wextra -mno-outline-atomics -I{kernel_dir} -I$(UNITY)/src -I$(PLATFORM_INCLUDE)

# Assembler flags
ASFLAGS = -c -O2 -Wall -Wextra -nostdlib -latomic -I{stubs_dir}

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

{runner_tasks}

# Clean build artifacts
clean:
	rm -rf {build_dir}

compile: $(RUNNER_BINS)


.PHONY: all clean qemu
"""

UNITY_PATH = os.environ.get("UNITY_PATH", "../../tools/Unity")

def join(files):
    return " ".join(files)

def main():
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
    levels = test_dir.split(os.sep)
    steps_back = [".."] * len(levels)
    relative_base = os.path.join(*steps_back)

    # making it relative to the test directory
    project_dir = os.path.join(relative_base, "..")
    kernel_dir = os.path.join(project_dir, "kernel")
    build_dir = os.path.join(relative_base, "build", test_dir)
    kernel_obj_build_dir = os.path.join(relative_base, "build")

    with open(manifest, "r") as manifest_file:
        manifest = yaml.load(manifest_file, Loader=yaml.FullLoader)
        source_files = manifest["manifest"]["source_files"]
        test_dir_relative_script = os.path.join(test_dir, project_dir)
        stubs_dir = os.path.join("kernel", "stubs")
        stubs_dir_relative = os.path.join(relative_base, stubs_dir)

        source_expanded = [f"{project_dir}/{expand[len(test_dir_relative_script) + 1:]}"
                           for file in source_files
                           for expand in glob.glob(os.path.join(test_dir_relative_script, file))]
        source_asm_files = [file
                            for file
                            in source_expanded
                            if file.endswith(".s")]
        stubs_c_files = [f"{relative_base}/{expand}"
                         for expand in glob.glob(os.path.join(stubs_dir, "*.c"))]
        stubs_asm_files = [f"{relative_base}/{expand}"
                           for expand in glob.glob(os.path.join(stubs_dir, "*.s"))]

        source_c_files = [file
                          for file
                          in source_expanded
                          if file.endswith(".c")]

        asm_objects = [os.path.join(kernel_obj_build_dir, file.replace(".s", ".o").replace("../", ""))
                       for file
                       in source_asm_files]

        c_objects = [os.path.join(kernel_obj_build_dir, file.replace(".c", ".o").replace("../", ""))
                     for file
                     in source_c_files]

        test_dir_c_files = [os.path.basename(file) for file in glob.glob(os.path.join(test_dir, "*.c"))]
        test_dir_asm_files = [os.path.basename(file) for file in glob.glob(os.path.join(test_dir, "*.s"))]

        stubs_dir_c_objects = [os.path.join(build_dir, file.replace(".c", ".o"))
                               for file
                               in stubs_c_files]
        stubs_dir_asm_objects = [os.path.join(build_dir, file.replace(".s", ".o"))
                                 for file
                                 in stubs_asm_files]

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
                                           kernel_sources=join([*source_c_files, *stubs_c_files]),
                                           kernel_asm_sources=join([*source_asm_files, *stubs_asm_files]),
                                           c_objects=join(test_dep_c_objects),
                                           asm_objects=join(test_dep_asm_objects),
                                           kernel_objects=join([*c_objects, *stubs_dir_c_objects]),
                                           kernel_asm_objects=join([*asm_objects, *stubs_dir_asm_objects]),
                                           unity_path=unity_path,
                                           test=os.path.basename(test_dir),
                                           kernel_dir=kernel_dir,
                                           project_base=project_dir,
                                           build_dir=build_dir,
                                           test_base=relative_base,
                                           stubs_dir=stubs_dir_relative,
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

{runner_bin}: $(ASM_OBJECTS) $(C_OBJECTS) $(KERNEL_ASM_OBJECTS) $(KERNEL_OBJECTS) {runner_obj} {test_obj}
	$(LD) $(LDFLAGS) $(UNITY)/libunity.a $^ -o $@

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
