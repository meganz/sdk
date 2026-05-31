#include "headers/BenchReportWriter.h"

#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <system_error>
#include <unordered_map>
#include <vector>

#if defined(__unix__) || defined(__APPLE__)
#include <unistd.h>
#endif

namespace mega::bench
{

namespace
{
std::mutex& reportMutex()
{
    static std::mutex m;
    return m;
}

std::vector<BenchReportCell>& storage()
{
    static std::vector<BenchReportCell> cells;
    return cells;
}

// Per-cell-name 0-based iter counter. Incremented inside `recordCell()` so
// each repeated invocation of the same cell name gets a distinct iter index
// without callers having to track it. `reset()` clears it.
std::unordered_map<std::string, std::int64_t>& iterCounters()
{
    static std::unordered_map<std::string, std::int64_t> m;
    return m;
}

std::string& jsonlReportDir()
{
    static std::string dir;
    return dir;
}

std::int64_t currentPid()
{
#if defined(__unix__) || defined(__APPLE__)
    return static_cast<std::int64_t>(getpid());
#else
    return 0;
#endif
}

// Minimal JSON escape — strings are short identifiers (test names, paths).
std::string jsonEscape(const std::string& s)
{
    std::string out;
    out.reserve(s.size() + 2);
    out.push_back('"');
    for (char c: s)
    {
        switch (c)
        {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20)
                {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned>(c));
                    out += buf;
                }
                else
                {
                    out.push_back(c);
                }
        }
    }
    out.push_back('"');
    return out;
}

// Serialize a single cell's fields into the JSON object body. The caller is
// responsible for the surrounding `{` and `}` (which differ between the
// consolidated array form and the single-line JSONL form).
void writeCellBody(std::ostream& ofs, const BenchReportCell& c, const char* indent)
{
    ofs << indent << "\"name\": " << jsonEscape(c.name) << ",";
    ofs << indent << "\"direction\": \""
        << (c.direction == Direction::Download ? "download" : "upload") << "\",";
    ofs << indent << "\"file_size_mib\": " << c.fileSizeMib << ",";
    ofs << indent << "\"connections\": " << c.connections << ",";
    ofs << indent << "\"iter\": " << c.iter << ",";
    ofs << indent << "\"duration_ms\": " << c.durationMs << ",";
    ofs << indent << "\"aggregate_kbps\": " << c.aggregateKbps << ",";
    ofs << indent << "\"first_byte_ms\": " << c.firstByteMs << ",";
    ofs << indent << "\"last_byte_ms\": " << c.lastByteMs << ",";
    ofs << indent << "\"rss_delta_kb\": " << c.rssCpuDelta.rssMaxKb << ",";
    ofs << indent << "\"user_cpu_ms\": " << c.rssCpuDelta.userCpuMs << ",";
    ofs << indent << "\"sys_cpu_ms\": " << c.rssCpuDelta.sysCpuMs << ",";
    ofs << indent << "\"chunk_ms_min\": " << c.chunkMsDist.min << ",";
    ofs << indent << "\"chunk_ms_max\": " << c.chunkMsDist.max << ",";
    ofs << indent << "\"chunk_ms_mean\": " << c.chunkMsDist.mean << ",";
    ofs << indent << "\"chunk_ms_median\": " << c.chunkMsDist.median << ",";
    ofs << indent << "\"chunk_ms_p95\": " << c.chunkMsDist.p95 << ",";
    ofs << indent << "\"chunk_n\": " << c.chunkMsDist.n;
}

// Build the absolute path `<reportDir>/bench_reports/<basename>_<PID>.<ext>`,
// ensuring the `bench_reports/` sub-directory exists. Returns empty on failure.
std::string buildBenchPath(const std::string& reportDir,
                           const char* basename,
                           const char* ext)
{
    if (reportDir.empty())
        return {};

    std::filesystem::path subdir = std::filesystem::path(reportDir) / "bench_reports";
    std::error_code ec;
    std::filesystem::create_directories(subdir, ec);
    if (ec)
        return {};

    std::ostringstream pathStream;
    pathStream << subdir.string();
    pathStream << '/' << basename << '_' << currentPid() << '.' << ext;
    return pathStream.str();
}

// Append one cell as a single-line JSON object to `path`. Each call opens, writes,
// flushes, and closes so the line survives a mid-run process kill.
void appendJsonlLine(const std::string& path, const BenchReportCell& c)
{
    std::ofstream ofs(path, std::ios::app);
    if (!ofs.is_open())
        return;

    ofs << '{';
    // No newlines between fields → entire cell on one line.
    writeCellBody(ofs, c, " ");
    ofs << " }\n";
    ofs.flush();
}

// Append one throttle-summary line to `throttle_summary_<PID>.jsonl`.
// Cross-references the bench JSONL by name + iter.
void appendThrottleSummaryLine(const std::string& path, const BenchReportCell& c)
{
    std::ofstream ofs(path, std::ios::app);
    if (!ofs.is_open())
        return;

    const double durationMs = static_cast<double>(c.durationMs);
    const double pressure =
        durationMs > 0
            ? static_cast<double>(c.throttleStats.event6TotalMs) / durationMs
            : 0.0;

    ofs << '{';
    ofs << " \"schema_version\": 1,";
    ofs << " \"name\": " << jsonEscape(c.name) << ",";
    ofs << " \"iter\": " << c.iter << ",";
    ofs << " \"duration_ms\": " << c.durationMs << ",";
    ofs << " \"throttle_event6_count\": " << c.throttleStats.event6Count << ",";
    ofs << " \"throttle_event6_total_ms\": " << c.throttleStats.event6TotalMs << ",";
    ofs << " \"wsupload_pause_count\": " << c.throttleStats.pauseCount << ",";
    ofs << " \"wsupload_pause_total_ms\": " << c.throttleStats.pauseTotalMs << ",";
    ofs << " \"throttle_pressure\": " << pressure;
    ofs << " }\n";
    ofs.flush();
}
} // namespace

BenchReportWriter& BenchReportWriter::instance()
{
    static BenchReportWriter inst;
    return inst;
}

void BenchReportWriter::setReportDir(const std::string& reportDir)
{
    std::lock_guard<std::mutex> g(reportMutex());
    jsonlReportDir() = reportDir;
    // Eagerly create the sub-directory so the consumer can rely on its presence
    // even before the first recordCell() arrives.
    if (!reportDir.empty())
    {
        std::error_code ec;
        std::filesystem::create_directories(
            std::filesystem::path(reportDir) / "bench_reports", ec);
    }
}

void BenchReportWriter::recordCell(const BenchReportCell& cell)
{
    std::lock_guard<std::mutex> g(reportMutex());

    // Assign a 0-based iter index per cell name so the JSONL artifacts'
    // (`name`, `iter`) tuple is unique and cross-references trivially. If the
    // caller has pre-populated `cell.iter` to a non-zero value, we still
    // overwrite it for consistency — there is one authoritative counter.
    BenchReportCell stored = cell;
    auto& counters = iterCounters();
    stored.iter = counters[stored.name]++;

    storage().push_back(stored);

    const std::string& dir = jsonlReportDir();
    if (dir.empty())
        return;

    const std::string benchPath = buildBenchPath(dir, "bench_report", "jsonl");
    if (!benchPath.empty())
    {
        appendJsonlLine(benchPath, stored);
    }

    // Sibling per-iter throttle artifact. Emitted alongside the bench JSONL
    // so cross-session comparisons can normalise against throttle exposure
    // (see BENCHMARKS.md "Throttle variance" note).
    const std::string throttlePath = buildBenchPath(dir, "throttle_summary", "jsonl");
    if (!throttlePath.empty())
    {
        appendThrottleSummaryLine(throttlePath, stored);
    }
}

void BenchReportWriter::reset()
{
    std::lock_guard<std::mutex> g(reportMutex());
    storage().clear();
    iterCounters().clear();
}

std::size_t BenchReportWriter::cellCount() const
{
    std::lock_guard<std::mutex> g(reportMutex());
    return storage().size();
}

std::string BenchReportWriter::flush(const std::string& reportDir)
{
    std::lock_guard<std::mutex> g(reportMutex());
    if (storage().empty() || reportDir.empty())
        return {};

    const std::string path = buildBenchPath(reportDir, "bench_report", "json");
    if (path.empty())
        return {};

    std::ofstream ofs(path, std::ios::trunc);
    if (!ofs.is_open())
        return {};

    ofs << "{\n";
    ofs << "  \"schema_version\": 2,\n";
    ofs << "  \"session_pid\": " << currentPid() << ",\n";
    ofs << "  \"cells\": [\n";
    for (std::size_t i = 0; i < storage().size(); ++i)
    {
        ofs << "    {";
        writeCellBody(ofs, storage()[i], "\n      ");
        ofs << "\n    }";
        if (i + 1 < storage().size())
            ofs << ",";
        ofs << "\n";
    }
    ofs << "  ]\n";
    ofs << "}\n";
    return path;
}

} // namespace mega::bench
