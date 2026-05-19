#include "headers/BenchReportWriter.h"

#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <system_error>
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

// Build the absolute path `<reportDir>/bench_reports/bench_report_<PID>.<ext>`,
// ensuring the `bench_reports/` sub-directory exists. Returns empty on failure.
std::string buildBenchPath(const std::string& reportDir, const char* ext)
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
    pathStream << '/' << "bench_report_" << currentPid() << '.' << ext;
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
    storage().push_back(cell);

    const std::string& dir = jsonlReportDir();
    if (dir.empty())
        return;

    const std::string path = buildBenchPath(dir, "jsonl");
    if (path.empty())
        return;

    appendJsonlLine(path, cell);
}

void BenchReportWriter::reset()
{
    std::lock_guard<std::mutex> g(reportMutex());
    storage().clear();
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

    const std::string path = buildBenchPath(reportDir, "json");
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
