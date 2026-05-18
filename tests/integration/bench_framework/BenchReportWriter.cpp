#include "headers/BenchReportWriter.h"

#include <fstream>
#include <mutex>
#include <sstream>
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
} // namespace

BenchReportWriter& BenchReportWriter::instance()
{
    static BenchReportWriter inst;
    return inst;
}

void BenchReportWriter::recordCell(const BenchReportCell& cell)
{
    std::lock_guard<std::mutex> g(reportMutex());
    storage().push_back(cell);
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

    const auto pid = currentPid();
    std::ostringstream pathStream;
    pathStream << reportDir;
    if (!reportDir.empty() && reportDir.back() != '/')
        pathStream << '/';
    pathStream << "bench_report_" << pid << ".json";
    const std::string path = pathStream.str();

    std::ofstream ofs(path, std::ios::trunc);
    if (!ofs.is_open())
        return {};

    ofs << "{\n";
    ofs << "  \"schema_version\": 2,\n";
    ofs << "  \"session_pid\": " << pid << ",\n";
    ofs << "  \"cells\": [\n";
    for (std::size_t i = 0; i < storage().size(); ++i)
    {
        const auto& c = storage()[i];
        ofs << "    {\n";
        ofs << "      \"name\": " << jsonEscape(c.name) << ",\n";
        ofs << "      \"direction\": \""
            << (c.direction == Direction::Download ? "download" : "upload") << "\",\n";
        ofs << "      \"file_size_mib\": " << c.fileSizeMib << ",\n";
        ofs << "      \"connections\": " << c.connections << ",\n";
        ofs << "      \"duration_ms\": " << c.durationMs << ",\n";
        ofs << "      \"aggregate_kbps\": " << c.aggregateKbps << ",\n";
        ofs << "      \"first_byte_ms\": " << c.firstByteMs << ",\n";
        ofs << "      \"last_byte_ms\": " << c.lastByteMs << ",\n";
        ofs << "      \"rss_delta_kb\": " << c.rssCpuDelta.rssMaxKb << ",\n";
        ofs << "      \"user_cpu_ms\": " << c.rssCpuDelta.userCpuMs << ",\n";
        ofs << "      \"sys_cpu_ms\": " << c.rssCpuDelta.sysCpuMs << ",\n";
        ofs << "      \"chunk_ms_min\": " << c.chunkMsDist.min << ",\n";
        ofs << "      \"chunk_ms_max\": " << c.chunkMsDist.max << ",\n";
        ofs << "      \"chunk_ms_mean\": " << c.chunkMsDist.mean << ",\n";
        ofs << "      \"chunk_ms_median\": " << c.chunkMsDist.median << ",\n";
        ofs << "      \"chunk_ms_p95\": " << c.chunkMsDist.p95 << ",\n";
        ofs << "      \"chunk_n\": " << c.chunkMsDist.n << "\n";
        ofs << "    }";
        if (i + 1 < storage().size())
            ofs << ",";
        ofs << "\n";
    }
    ofs << "  ]\n";
    ofs << "}\n";
    return path;
}

} // namespace mega::bench
