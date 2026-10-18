#include "interpreter.h"

#include "../../lib/wuff/filesystem.h"

static BString cleanup_failed = bstr("Unable to clean up dynamic program mappings");

WuffInterpreterProgram WuffInterpreterProgram__new(WuffGraph graph, Address entry)
{
    return (WuffInterpreterProgram){
        .graph = graph,
        .entry = entry,
    };
}

static Result(WuffInterpreterProgram) WuffInterpreter__load_root(ConstWuffImagePtr root)
{
    WuffMemoryOperations memory = WuffMemoryOperations__system();
    WuffDependencyOperations dependencies = WuffFilesystem__dependencies();
    Result(WuffGraph) loaded = WuffLoader__load_graph(root, &memory, &dependencies);
    WuffFilesystem__release_image(root);

    match_result(
        loaded,
        success(graph)
        {
            match_result(
                WuffGraph__entry(&graph),
                success(entry)
                {
                    WuffInterpreterProgram program = WuffInterpreterProgram__new(graph, entry);
                    return Success(WuffInterpreterProgram, program);
                },
                error(code, message)
                {
                    match_result(
                        WuffGraph__dispose(&graph),
                        success_do_nothing(),
                        error(cleanup_code, cleanup_message)
                        {
                            return Error(WuffInterpreterProgram, cleanup_failed, WUFF_LOAD_CLEANUP_FAILED);
                        }
                    );
                    return Error(WuffInterpreterProgram, message, code);
                }
            );
        },
        error(code, message)
        {
            return Error(WuffInterpreterProgram, message, code);
        }
    );
    return Error(WuffInterpreterProgram, bstr("Unknown error"), RESULT_UNKNOWN_ERROR_CODE);
}

Result(WuffInterpreterProgram) WuffInterpreter__load(ConstBStringPtr path)
{
    match_result(
        WuffFilesystem__load_image(path),
        success(image)
        {
            return WuffInterpreter__load_root(&image);
        },
        error(code, message)
        {
            return Error(WuffInterpreterProgram, message, code);
        }
    );
    return Error(WuffInterpreterProgram, bstr("Unknown error"), RESULT_UNKNOWN_ERROR_CODE);
}

Address WuffInterpreterProgram__entry(ConstWuffInterpreterProgramPtr self)
{
    return self->entry;
}

WuffGraphPtr WuffInterpreterProgram__graph(WuffInterpreterProgramPtr self)
{
    return &self->graph;
}
