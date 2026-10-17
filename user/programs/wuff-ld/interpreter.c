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

Result(WuffInterpreterProgram) WuffInterpreter__load(ConstBStringPtr path)
{
    WuffImage root;
    match_result(
        WuffFilesystem__load_image(path),
        success(image)
        {
            root = image;
        },
        error(code, message)
        {
            return Error(WuffInterpreterProgram, message, code);
        }
    );

    WuffMemoryOperations memory = WuffMemoryOperations__system();
    WuffDependencyOperations dependencies = WuffFilesystem__dependencies();
    Result(WuffGraph) loaded = WuffLoader__load_graph(&root, &memory, &dependencies);
    WuffFilesystem__release_image(&root);

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
}

Address WuffInterpreterProgram__entry(ConstWuffInterpreterProgramPtr self)
{
    return self->entry;
}

WuffGraphPtr WuffInterpreterProgram__graph(WuffInterpreterProgramPtr self)
{
    return &self->graph;
}
