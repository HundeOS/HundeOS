#include "../../user.h"

#include "ipc/io_diagnostics.h"
#include "ipc/ipc_platform.h"
#include "process/process_registry.h"
#include "sys/io.h"
#include "sys/process.h"
#include "sys/process_metrics.h"
#include "trace/io_trace.h"
#include "types/bstring_additional_types.h"
#include "types/fixed_array.h"

#define FS_LATENCY_DEFAULT_PATH "/usr/local/bin/cp"
#define FS_LATENCY_DEFAULT_SAMPLES 16ULL
#define FS_LATENCY_MAX_SAMPLES 1000ULL
#define FS_LATENCY_READ_SIZE 4096U
#define FS_LATENCY_HOP_LIMIT 64U
#define FS_LATENCY_SPAN_LIMIT 1024U
#define FS_LATENCY_CHAIN_LIMIT 64U

typedef struct FSLatencySummary
{
    uint64_t samples;
    uint64_t failures;
    uint64_t bytes;
    IODiagnosticLatency total;
    IODiagnosticLatency open;
    IODiagnosticLatency read;
    IODiagnosticLatency close;
} FSLatencySummary;

typedef FSLatencySummary * FSLatencySummaryPtr;

typedef struct FSLatencyHop
{
    uint64_t client_pid;
    uint64_t server_pid;
    uint64_t samples;
    IODiagnosticLatency latency;
} FSLatencyHop;

typedef FSLatencyHop * FSLatencyHopPtr;
OptionalDef(FSLatencyHop);

static FSLatencyHop FSLatencyHop__new(uint64_t client_pid, uint64_t server_pid)
{
    return (FSLatencyHop){.client_pid = client_pid, .server_pid = server_pid};
}

static bool FSLatencyHop__equals(void * first, void * second)
{
    FSLatencyHopPtr left = first;
    FSLatencyHopPtr right = second;
    return left->client_pid == right->client_pid && left->server_pid == right->server_pid;
}

FixedArrayDefine(FSLatencyHop, FS_LATENCY_HOP_LIMIT, FSLatencyHops, &FSLatencyHop__equals);
FixedArrayStackBodyDefine(FSLatencyHops);
typedef FSLatencyHops * FSLatencyHopsPtr;

typedef struct FSLatencyHopSample
{
    uint64_t client_pid;
    uint64_t server_pid;
    uint64_t origin_pid;
    uint64_t published_at_usec;
    uint64_t queue_usec;
} FSLatencyHopSample;

OptionalDef(FSLatencyHopSample);

typedef struct FSLatencySpan
{
    uint64_t client_pid;
    uint64_t server_pid;
    uint64_t connection;
    uint64_t generation;
    uint64_t origin_pid;
    uint64_t parent_connection;
    uint64_t parent_generation;
    uint64_t published_at_usec;
    uint64_t received_at_usec;
    uint64_t replied_at_usec;
    uint64_t completed_at_usec;
} FSLatencySpan;

typedef FSLatencySpan * FSLatencySpanPtr;
typedef const FSLatencySpan * ConstFSLatencySpanPtr;
OptionalDef(FSLatencySpan);

static bool FSLatencySpan__equals(void * first, void * second)
{
    FSLatencySpanPtr left = first;
    FSLatencySpanPtr right = second;
    return left->connection == right->connection && left->generation == right->generation &&
           left->origin_pid == right->origin_pid;
}

FixedArrayDefine(FSLatencySpan, FS_LATENCY_SPAN_LIMIT, FSLatencySpans, &FSLatencySpan__equals);
FixedArrayStackBodyDefine(FSLatencySpans);
typedef FSLatencySpans * FSLatencySpansPtr;

typedef struct FSLatencyChain
{
    uint64_t parent_client_pid;
    uint64_t parent_server_pid;
    uint64_t child_server_pid;
    uint64_t samples;
    uint64_t resume_samples;
    IODiagnosticLatency pickup_to_emit;
    IODiagnosticLatency downstream;
    IODiagnosticLatency resume_to_reply;
} FSLatencyChain;

typedef FSLatencyChain * FSLatencyChainPtr;
OptionalDef(FSLatencyChain);

static bool FSLatencyChain__equals(void * first, void * second)
{
    FSLatencyChainPtr left = first;
    FSLatencyChainPtr right = second;
    return left->parent_client_pid == right->parent_client_pid && left->parent_server_pid == right->parent_server_pid &&
           left->child_server_pid == right->child_server_pid;
}

FixedArrayDefine(FSLatencyChain, FS_LATENCY_CHAIN_LIMIT, FSLatencyChains, &FSLatencyChain__equals);
FixedArrayStackBodyDefine(FSLatencyChains);
typedef FSLatencyChains * FSLatencyChainsPtr;

static FSLatencyHopSample FSLatencyHopSample__new(void)
{
    return (FSLatencyHopSample){0};
}

static FSLatencySpan FSLatencySpan__new(void)
{
    return (FSLatencySpan){0};
}

static FSLatencyChain FSLatencyChain__new(uint64_t parent_client_pid, uint64_t parent_server_pid,
                                          uint64_t child_server_pid)
{
    return (FSLatencyChain){.parent_client_pid = parent_client_pid,
                            .parent_server_pid = parent_server_pid,
                            .child_server_pid = child_server_pid};
}

static FSLatencySummary FSLatencySummary__new(void)
{
    return (FSLatencySummary){0};
}

static uint64_t FSLatency__delta(uint64_t after, uint64_t before)
{
    return after >= before ? after - before : 0;
}

static uint64_t FSLatency__bounded_delta(uint64_t whole, uint64_t part)
{
    return whole >= part ? whole - part : 0;
}

static void FSLatency__print_latency(ConstBStringPtr name, ConstIODiagnosticLatencyPtr latency)
{
    IO__printf(&bstr("%B_usec p50=%llu p70=%llu p90=%llu p99=%llu\n"), name,
               IODiagnosticLatency__percentile(latency, 50), IODiagnosticLatency__percentile(latency, 70),
               IODiagnosticLatency__percentile(latency, 90), IODiagnosticLatency__percentile(latency, 99));
}

static Optional(uint64_t) FSLatency__parse_hop_field(ConstBStringPtr token, ConstBStringPtr prefix)
{
    if (!BString__starts_with(token, prefix))
    {
        return None(uint64_t);
    }
    BString value = BString__slice(token, BString__length(prefix), BString__length(token));
    return BString__parse_uint64(&value);
}

static Optional(FSLatencyHopSample) FSLatency__parse_hop(ConstBStringPtr line)
{
    if (!BString__starts_with(line, &bstr("IO-HOP ")))
    {
        return None(FSLatencyHopSample);
    }
    FSLatencyHopSample sample = FSLatencyHopSample__new();
    BStringArray tokens = BString__split(line, ' ');
    for_each_fa_ptr(
        BStringArray,
        &tokens,
        index,
        token,
        {
            sample.client_pid =
                unwrap_optional(FSLatency__parse_hop_field(&token, &bstr("client=")), sample.client_pid);
            sample.server_pid =
                unwrap_optional(FSLatency__parse_hop_field(&token, &bstr("server=")), sample.server_pid);
            sample.origin_pid =
                unwrap_optional(FSLatency__parse_hop_field(&token, &bstr("origin=")), sample.origin_pid);
            sample.published_at_usec = unwrap_optional(
                FSLatency__parse_hop_field(&token, &bstr("published_at_usec=")), sample.published_at_usec);
            sample.queue_usec = unwrap_optional(FSLatency__parse_hop_field(&token, &bstr("queue_usec=")),
                                                sample.queue_usec);
        }
    );
    return sample.client_pid != 0 && sample.server_pid != 0 && sample.origin_pid != 0 && sample.published_at_usec != 0
               ? Some(FSLatencyHopSample, sample)
               : None(FSLatencyHopSample);
}

static Optional(FSLatencySpan) FSLatency__parse_span(ConstBStringPtr line)
{
    if (!BString__starts_with(line, &bstr("IO-LATENCY ")))
    {
        return None(FSLatencySpan);
    }
    FSLatencySpan span = FSLatencySpan__new();
    BStringArray tokens = BString__split(line, ' ');
    for_each_fa_ptr(
        BStringArray,
        &tokens,
        index,
        token,
        {
            span.client_pid = unwrap_optional(FSLatency__parse_hop_field(&token, &bstr("client=")), span.client_pid);
            span.server_pid = unwrap_optional(FSLatency__parse_hop_field(&token, &bstr("server=")), span.server_pid);
            span.connection =
                unwrap_optional(FSLatency__parse_hop_field(&token, &bstr("connection=")), span.connection);
            span.generation =
                unwrap_optional(FSLatency__parse_hop_field(&token, &bstr("generation=")), span.generation);
            span.origin_pid = unwrap_optional(FSLatency__parse_hop_field(&token, &bstr("origin=")), span.origin_pid);
            span.parent_connection = unwrap_optional(FSLatency__parse_hop_field(&token, &bstr("parent_connection=")),
                                                     span.parent_connection);
            span.parent_generation = unwrap_optional(FSLatency__parse_hop_field(&token, &bstr("parent_generation=")),
                                                     span.parent_generation);
            span.published_at_usec = unwrap_optional(FSLatency__parse_hop_field(&token, &bstr("published_at_usec=")),
                                                     span.published_at_usec);
            span.received_at_usec =
                unwrap_optional(FSLatency__parse_hop_field(&token, &bstr("received_at_usec=")), span.received_at_usec);
            span.replied_at_usec =
                unwrap_optional(FSLatency__parse_hop_field(&token, &bstr("replied_at_usec=")), span.replied_at_usec);
            span.completed_at_usec = unwrap_optional(FSLatency__parse_hop_field(&token, &bstr("completed_at_usec=")),
                                                     span.completed_at_usec);
        }
    );

    bool valid_identity = span.client_pid != 0 && span.server_pid != 0 && span.connection != 0 &&
                          span.generation != 0 && span.origin_pid != 0;
    bool valid_timestamps = span.published_at_usec != 0 && span.published_at_usec <= span.received_at_usec &&
                            span.received_at_usec <= span.replied_at_usec &&
                            span.replied_at_usec <= span.completed_at_usec;
    return valid_identity && valid_timestamps ? Some(FSLatencySpan, span) : None(FSLatencySpan);
}

static void FSLatencyHops__record_line(FSLatencyHopsPtr self, ConstBStringPtr line, uint64_t origin_pid,
                                       uint64_t started_at_usec, uint64_t completed_at_usec)
{
    match_optional(
        FSLatency__parse_hop(line),
        some(sample)
        {
            if (sample.origin_pid != origin_pid || sample.published_at_usec < started_at_usec ||
                sample.published_at_usec > completed_at_usec)
            {
                return;
            }
            FSLatencyHop key = FSLatencyHop__new(sample.client_pid, sample.server_pid);
            match_optional(
                FSLatencyHops__find_item(self, key),
                some(index)
                {
                    match_optional(
                        FSLatencyHops__ptr_at(self, index),
                        some(hop)
                        {
                            hop->samples++;
                            IODiagnosticLatency__record(&hop->latency, sample.queue_usec);
                        },
                        none_do_nothing()
                    );
                },
                none()
                {
                    key.samples = 1;
                    IODiagnosticLatency__record(&key.latency, sample.queue_usec);
                    match_result(
                        FSLatencyHops__push(self, key),
                        success_do_nothing(),
                        error(code, message)
                        {
                            IO__printf(&bstr("io_hop unavailable: %B (%i)\n"), &message, code);
                            return;
                        }
                    );
                }
            );
        },
        none_do_nothing()
    );
}

static void FSLatencySpans__record_line(FSLatencySpansPtr self, ConstBStringPtr line, uint64_t origin_pid,
                                        uint64_t started_at_usec, uint64_t completed_at_usec)
{
    match_optional(
        FSLatency__parse_span(line),
        some(span)
        {
            if (span.origin_pid != origin_pid || span.published_at_usec < started_at_usec ||
                span.published_at_usec > completed_at_usec)
            {
                return;
            }
            match_result(
                FSLatencySpans__push(self, span),
                success_do_nothing(),
                error(code, message)
                {
                    IO__printf(&bstr("io_span unavailable: %B (%i)\n"), &message, code);
                    return;
                }
            );
        },
        none_do_nothing()
    );
}

static void FSLatency__read_trace(FSLatencyHopsPtr hops, FSLatencySpansPtr spans, uint64_t origin_pid,
                                  uint64_t started_at_usec, uint64_t completed_at_usec, uint64_t start_offset,
                                  uint64_t end_offset)
{
    if (end_offset <= start_offset)
    {
        return;
    }

    IOHandle trace = IO__open(&bstr(IO_TRACE_LOG_PATH), IO_OPEN_READ);
    if (trace < 0)
    {
        return;
    }
    IOSeekResult seek = IO__seek(trace, (int64_t)start_offset, IO_SEEK_SET);
    if (seek.status != IO_OK || seek.position != start_offset)
    {
        IO__close(trace);
        return;
    }

    BString line = empty_bstr();
    uint64_t remaining = end_offset - start_offset;
    uint8_t data[FS_LATENCY_READ_SIZE];
    while (remaining > 0)
    {
        size_t capacity = remaining < sizeof(data) ? (size_t)remaining : sizeof(data);
        ByteCount count = IO__read_bytes(trace, (char *)data, capacity);
        if (count <= 0)
        {
            break;
        }
        for (ByteCount index = 0; index < count; index++)
        {
            if (data[index] == '\n')
            {
                FSLatencyHops__record_line(hops, &line, origin_pid, started_at_usec, completed_at_usec);
                FSLatencySpans__record_line(spans, &line, origin_pid, started_at_usec, completed_at_usec);
                BString__clear(&line);
            }
            else
            {
                BString__push_char(&line, data[index]);
            }
        }
        remaining -= (uint64_t)count;
    }

    if (!BString__is_empty(&line))
    {
        FSLatencyHops__record_line(hops, &line, origin_pid, started_at_usec, completed_at_usec);
        FSLatencySpans__record_line(spans, &line, origin_pid, started_at_usec, completed_at_usec);
    }
    IO__close(trace);
}

static void FSLatencyHops__print(FSLatencyHopsPtr self)
{
    for_each_fa_ptr(
        FSLatencyHops,
        self,
        index,
        value,
        {
            match_optional(
                FSLatencyHops__ptr_at(self, index),
                some(hop)
                {
                    IO__printf(
                        &bstr("io_hop client=%llu server=%llu samples=%llu p50_usec=%llu p70_usec=%llu "
                              "p90_usec=%llu p99_usec=%llu\n"),
                        hop->client_pid, hop->server_pid, hop->samples,
                        IODiagnosticLatency__percentile(&hop->latency, 50),
                        IODiagnosticLatency__percentile(&hop->latency, 70),
                        IODiagnosticLatency__percentile(&hop->latency, 90),
                        IODiagnosticLatency__percentile(&hop->latency, 99));
                },
                none_do_nothing()
            );
        }
    );
}

static void FSLatencyChains__record(FSLatencyChainsPtr self, ConstFSLatencySpanPtr parent, ConstFSLatencySpanPtr child,
                                    uint64_t pickup_to_emit_usec, OptionalPtr(uint64_t) resume_to_reply_usec)
{
    FSLatencyChain key = FSLatencyChain__new(parent->client_pid, parent->server_pid, child->server_pid);
    match_optional(
        FSLatencyChains__find_item(self, key),
        some(index)
        {
            match_optional(
                FSLatencyChains__ptr_at(self, index),
                some(chain)
                {
                    chain->samples++;
                    IODiagnosticLatency__record(&chain->pickup_to_emit, pickup_to_emit_usec);
                    IODiagnosticLatency__record(&chain->downstream,
                                                child->completed_at_usec - child->published_at_usec);
                    match_optional(
                        *resume_to_reply_usec,
                        some(resume)
                        {
                            chain->resume_samples++;
                            IODiagnosticLatency__record(&chain->resume_to_reply, resume);
                        },
                        none_do_nothing()
                    );
                },
                none_do_nothing()
            );
        },
        none()
        {
            key.samples = 1;
            IODiagnosticLatency__record(&key.pickup_to_emit, pickup_to_emit_usec);
            IODiagnosticLatency__record(&key.downstream, child->completed_at_usec - child->published_at_usec);
            match_optional(
                *resume_to_reply_usec,
                some(resume)
                {
                    key.resume_samples = 1;
                    IODiagnosticLatency__record(&key.resume_to_reply, resume);
                },
                none_do_nothing()
            );
            match_result(
                FSLatencyChains__push(self, key),
                success_do_nothing(),
                error(code, message)
                {
                    IO__printf(&bstr("io_chain unavailable: %B (%i)\n"), &message, code);
                    return;
                }
            );
        }
    );
}

static FSLatencyChains FSLatencySpans__chains(FSLatencySpansPtr self)
{
    FSLatencyChains chains = FSLatencyChains__new();
    for_each_fa_ptr(
        FSLatencySpans,
        self,
        child_index,
        child,
        {
            if (child.parent_connection == 0 || child.parent_generation == 0)
            {
                continue;
            }
            FSLatencySpan parent_key = FSLatencySpan__new();
            parent_key.connection = child.parent_connection;
            parent_key.generation = child.parent_generation;
            parent_key.origin_pid = child.origin_pid;
            match_optional(
                FSLatencySpans__find_item(self, parent_key),
                some(parent_index)
                {
                    match_optional(
                        FSLatencySpans__ptr_at(self, parent_index),
                        some(parent)
                        {
                            if (child.published_at_usec < parent->received_at_usec ||
                                child.completed_at_usec > parent->replied_at_usec)
                            {
                                continue;
                            }
                            uint64_t previous_completion_usec = parent->received_at_usec;
                            bool later_child = false;
                            for_each_fa_ptr(
                                FSLatencySpans,
                                self,
                                sibling_index,
                                sibling,
                                {
                                    bool same_parent =
                                        sibling.origin_pid == child.origin_pid &&
                                        sibling.parent_connection == child.parent_connection &&
                                        sibling.parent_generation == child.parent_generation;
                                    if (!same_parent)
                                    {
                                        continue;
                                    }
                                    if (sibling.completed_at_usec <= child.published_at_usec &&
                                        sibling.completed_at_usec > previous_completion_usec)
                                    {
                                        previous_completion_usec = sibling.completed_at_usec;
                                    }
                                    if (sibling.published_at_usec > child.published_at_usec &&
                                        sibling.completed_at_usec <= parent->replied_at_usec)
                                    {
                                        later_child = true;
                                    }
                                }
                            );
                            Optional(uint64_t) resume_to_reply =
                                later_child
                                    ? None(uint64_t)
                                    : Some(uint64_t, parent->replied_at_usec - child.completed_at_usec);
                            FSLatencyChains__record(&chains, parent, &child,
                                                    child.published_at_usec - previous_completion_usec,
                                                    &resume_to_reply);
                        },
                        none_do_nothing()
                    );
                },
                none_do_nothing()
            );
        }
    );
    return chains;
}

static void FSLatencyChains__print(FSLatencyChainsPtr self)
{
    for_each_fa_ptr(
        FSLatencyChains,
        self,
        index,
        value,
        {
            match_optional(
                FSLatencyChains__ptr_at(self, index),
                some(chain)
                {
                    IO__printf(&bstr("io_chain parent_client=%llu parent_server=%llu child_server=%llu samples=%llu "
                                     "pickup_to_emit_p50_usec=%llu pickup_to_emit_p70_usec=%llu "
                                     "pickup_to_emit_p90_usec=%llu pickup_to_emit_p99_usec=%llu "
                                     "downstream_p50_usec=%llu downstream_p70_usec=%llu downstream_p90_usec=%llu "
                                     "downstream_p99_usec=%llu resume_samples=%llu resume_to_reply_p50_usec=%llu "
                                     "resume_to_reply_p70_usec=%llu resume_to_reply_p90_usec=%llu "
                                     "resume_to_reply_p99_usec=%llu\n"),
                               chain->parent_client_pid, chain->parent_server_pid, chain->child_server_pid,
                               chain->samples, IODiagnosticLatency__percentile(&chain->pickup_to_emit, 50),
                               IODiagnosticLatency__percentile(&chain->pickup_to_emit, 70),
                               IODiagnosticLatency__percentile(&chain->pickup_to_emit, 90),
                               IODiagnosticLatency__percentile(&chain->pickup_to_emit, 99),
                               IODiagnosticLatency__percentile(&chain->downstream, 50),
                               IODiagnosticLatency__percentile(&chain->downstream, 70),
                               IODiagnosticLatency__percentile(&chain->downstream, 90),
                               IODiagnosticLatency__percentile(&chain->downstream, 99), chain->resume_samples,
                               IODiagnosticLatency__percentile(&chain->resume_to_reply, 50),
                               IODiagnosticLatency__percentile(&chain->resume_to_reply, 70),
                               IODiagnosticLatency__percentile(&chain->resume_to_reply, 90),
                               IODiagnosticLatency__percentile(&chain->resume_to_reply, 99));
                },
                none_do_nothing()
            );
        }
    );
}

static bool FSLatency__sample(FSLatencySummaryPtr self, ConstBStringPtr path)
{
    uint8_t data[FS_LATENCY_READ_SIZE];
    uint64_t total_started = IpcPlatform__monotonic_usec();
    uint64_t started = total_started;
    IOHandle handle = IO__open(path, IO_OPEN_READ);
    IODiagnosticLatency__record(&self->open, IpcPlatform__monotonic_usec() - started);
    if (handle < 0)
    {
        self->failures++;
        return false;
    }

    started = IpcPlatform__monotonic_usec();
    ByteCount bytes = IO__read_bytes(handle, (char *)data, sizeof(data));
    IODiagnosticLatency__record(&self->read, IpcPlatform__monotonic_usec() - started);

    started = IpcPlatform__monotonic_usec();
    IOStatus close_status = IO__close(handle);
    IODiagnosticLatency__record(&self->close, IpcPlatform__monotonic_usec() - started);
    IODiagnosticLatency__record(&self->total, IpcPlatform__monotonic_usec() - total_started);
    if (bytes < 0 || close_status != IO_OK)
    {
        self->failures++;
        return false;
    }

    self->samples++;
    self->bytes += (uint64_t)bytes;
    return true;
}

static Optional(ProcessRecord) FSLatency__process_snapshot(ConstProcessRegistryPtr registry,
                                                           uint64_t sampled_after_usec)
{
    match_result(
        ProcessMetrics__snapshot_after(registry, process_current_pid(), sampled_after_usec),
        success(record)
        {
            return Some(ProcessRecord, record);
        },
        error(code, message)
        {
            return None(ProcessRecord);
        }
    );
}

int main(int argc, char ** argv)
{
    if (argc > 3)
    {
        IO__print(&bstr("usage: fs-latency [path] [samples]\n"));
        return 1;
    }

    BString path = argc >= 2 ? BString__new(argv[1]) : bstr(FS_LATENCY_DEFAULT_PATH);
    uint64_t sample_count = FS_LATENCY_DEFAULT_SAMPLES;
    if (argc == 3)
    {
        BString raw_samples = BString__new(argv[2]);
        match_optional(
            BString__parse_uint64(&raw_samples),
            some(parsed)
            {
                sample_count = parsed;
            },
            none()
            {
                return 1;
            }
        );
    }

    if (sample_count == 0 || sample_count > FS_LATENCY_MAX_SAMPLES)
    {
        return 1;
    }

    uint64_t pid = process_current_pid();
    ConstProcessRegistryPtr registry;
    match_result(
        process_registry_map(),
        success(mapped)
        {
            registry = mapped;
        },
        error(code, message)
        {
            IO__printf(&bstr("cannot map process registry: %B\n"), &message);
            return 1;
        }
    );
    uint64_t trace_start_offset = 0;
    match_result(
        IO__file_info(&bstr(IO_TRACE_LOG_PATH)),
        success(info)
        {
            trace_start_offset = info.size;
        },
        error(code, message)
        {
            trace_start_offset = 0;
        }
    );
    Result(ProcessInfo) before_info = ProcessMetrics__info(pid);
    Optional(ProcessRecord) before = FSLatency__process_snapshot(registry, 0);
    uint64_t started = IpcPlatform__monotonic_usec();
    FSLatencySummary summary = FSLatencySummary__new();
    for (uint64_t sample = 0; sample < sample_count; sample++)
    {
        FSLatency__sample(&summary, &path);
    }
    uint64_t completed = IpcPlatform__monotonic_usec();
    Result(ProcessInfo) after_info = ProcessMetrics__info(pid);
    Optional(ProcessRecord) after = FSLatency__process_snapshot(registry, completed);
    TraceStatus trace_drain = IOTrace__drain();
    uint64_t trace_end_offset = trace_start_offset;
    match_result(
        IO__file_info(&bstr(IO_TRACE_LOG_PATH)),
        success(info)
        {
            trace_end_offset = info.size;
        },
        error(code, message)
        {
            trace_end_offset = trace_start_offset;
        }
    );
    FSLatencyHops hops = FSLatencyHops__new();
    FSLatencySpans spans = FSLatencySpans__new();
    FSLatency__read_trace(&hops, &spans, pid, started, completed, trace_start_offset, trace_end_offset);
    FSLatencyChains chains = FSLatencySpans__chains(&spans);

    IO__printf(
        &bstr("fs-latency pid=%llu path=%B samples=%llu failures=%llu bytes=%llu wall_usec=%llu trace_drain=%i\n"), pid,
        &path, summary.samples, summary.failures, summary.bytes, completed - started, trace_drain);
    FSLatency__print_latency(&bstr("total"), &summary.total);
    FSLatency__print_latency(&bstr("open"), &summary.open);
    FSLatency__print_latency(&bstr("read"), &summary.read);
    FSLatency__print_latency(&bstr("close"), &summary.close);
    FSLatencyHops__print(&hops);
    FSLatencyChains__print(&chains);
    match_result(
        before_info,
        success(before_process)
        {
            match_result(
                after_info,
                success(after_process)
                {
                    uint64_t wall_usec = completed - started;
                    uint64_t active_usec =
                        FSLatency__delta(after_process.cpu_usage.total_usec, before_process.cpu_usage.total_usec);
                    uint64_t scheduled_usec = FSLatency__delta(after_process.cpu_usage.scheduled_usec,
                                                               before_process.cpu_usage.scheduled_usec);
                    uint64_t off_cpu_usec = FSLatency__bounded_delta(wall_usec, scheduled_usec);
                    uint64_t scheduled_wait_usec = FSLatency__bounded_delta(scheduled_usec, active_usec);
                    uint64_t user_wait_usec =
                        FSLatency__delta(after_process.user_wait_usec, before_process.user_wait_usec);
                    IO__printf(&bstr("process_active_usec=%llu process_scheduled_wait_usec=%llu "
                                     "process_off_cpu_usec=%llu process_scheduled_usec=%llu user_wait_usec=%llu\n"),
                               active_usec, scheduled_wait_usec, off_cpu_usec, scheduled_usec, user_wait_usec);
                },
                error(code, message)
                {
                    IO__printf(&bstr("process metrics unavailable: %B\n"), &message);
                }
            );
        },
        error(code, message)
        {
            IO__printf(&bstr("process metrics unavailable: %B\n"), &message);
        }
    );
    match_optional(
        before,
        some(before_record)
        {
            match_optional(
                after,
                some(after_record)
                {
                    ProcessQueueWaitUsage before_wait = before_record.queue_wait_usage;
                    ProcessQueueWaitUsage after_wait = after_record.queue_wait_usage;
                    IO__printf(&bstr("queue_parked_usec=%llu queue_runnable_usec=%llu selections=%llu\n"),
                               FSLatency__delta(after_wait.parked_usec, before_wait.parked_usec),
                               FSLatency__delta(after_wait.runnable_usec, before_wait.runnable_usec),
                               FSLatency__delta(after_wait.selections, before_wait.selections));
                },
                none_do_nothing()
            );
        },
        none_do_nothing()
    );
    return summary.failures == 0 && summary.samples == sample_count ? 0 : 1;
}
