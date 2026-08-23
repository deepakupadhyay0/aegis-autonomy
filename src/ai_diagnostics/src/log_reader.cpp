#include "ai_diagnostics/log_reader.hpp"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <sys/stat.h>

namespace ai_diagnostics
{
namespace
{

constexpr std::size_t TIMESTAMP_SIZE = 30U;
constexpr std::string_view FIELD_SEPARATOR = ": ";

struct level_marker_s
{
  std::string_view marker;
  logging::log_level_e level;
};

constexpr level_marker_s LEVEL_MARKERS[] = {
  {": DEBUG: ", logging::log_level_e::debug},
  {": INFO: ", logging::log_level_e::info},
  {": WARN: ", logging::log_level_e::warning},
  {": ERROR: ", logging::log_level_e::error},
  {": FATAL: ", logging::log_level_e::fatal}};

bool is_log_filename(const std::filesystem::path & path)
{
  const std::string filename = path.filename().string();
  if (filename.ends_with(".log")) {
    return true;
  }
  const std::size_t log_marker = filename.rfind(".log.");
  if (log_marker == std::string::npos || log_marker + 5U >= filename.size()) {
    return false;
  }
  return std::all_of(
    filename.begin() + static_cast<std::ptrdiff_t>(log_marker + 5U),
    filename.end(),
    [](const char value) {return value >= '0' && value <= '9';});
}

std::string node_name_from_path(const std::filesystem::path & path)
{
  std::string filename = path.filename().string();
  if (filename.ends_with(".log")) {
    filename.resize(filename.size() - 4U);
  } else {
    const std::size_t log_marker = filename.rfind(".log.");
    if (log_marker != std::string::npos) {
      filename.resize(log_marker);
    }
  }
  const std::size_t rotation_separator = filename.rfind('.');
  if (rotation_separator != std::string::npos &&
    rotation_separator + 1U < filename.size() &&
    std::all_of(
      filename.begin() +
      static_cast<std::ptrdiff_t>(rotation_separator + 1U),
      filename.end(),
      [](const char value) {return value >= '0' && value <= '9';}))
  {
    filename.resize(rotation_separator);
  }
  return filename;
}

bool read_file_identity(
  const std::filesystem::path & path,
  common::uint64_t & device,
  common::uint64_t & inode) noexcept
{
  struct stat status {};
  if (::stat(path.c_str(), &status) != 0 || !S_ISREG(status.st_mode)) {
    return false;
  }
  device = static_cast<common::uint64_t>(status.st_dev);
  inode = static_cast<common::uint64_t>(status.st_ino);
  return true;
}

bool parse_source_location(
  const std::string_view value,
  common::string64_t & source_file,
  common::uint32_t & source_line) noexcept
{
  const std::size_t separator = value.rfind(':');
  if (separator == std::string_view::npos || separator + 1U >= value.size()) {
    return false;
  }
  common::uint32_t parsed_line = 0U;
  const std::from_chars_result result = std::from_chars(
    value.data() + separator + 1U,
    value.data() + value.size(),
    parsed_line);
  if (result.ec != std::errc{} || result.ptr != value.data() + value.size()) {
    return false;
  }
  source_file.assign(value.substr(0U, separator));
  source_line = parsed_line;
  return !source_file.empty();
}

}  // namespace

bool parse_log_line(
  const std::string_view node_name,
  const std::string_view evidence_id,
  const std::string_view line,
  parsed_log_record_s & record) noexcept
{
  if (node_name.empty() || line.size() <= TIMESTAMP_SIZE + 2U ||
    line[TIMESTAMP_SIZE - 1U] != 'Z' ||
    line.substr(TIMESTAMP_SIZE, FIELD_SEPARATOR.size()) != FIELD_SEPARATOR)
  {
    return false;
  }

  const std::size_t fields_begin = TIMESTAMP_SIZE + FIELD_SEPARATOR.size();
  std::size_t marker_position = std::string_view::npos;
  const level_marker_s * selected_marker = nullptr;
  for (const level_marker_s & candidate : LEVEL_MARKERS) {
    const std::size_t position = line.find(candidate.marker, fields_begin);
    if (position != std::string_view::npos &&
      (selected_marker == nullptr || position < marker_position))
    {
      marker_position = position;
      selected_marker = &candidate;
    }
  }
  if (selected_marker == nullptr || marker_position == fields_begin) {
    return false;
  }

  const std::size_t location_begin =
    marker_position + selected_marker->marker.size();
  const std::size_t message_separator = line.find(
    FIELD_SEPARATOR,
    location_begin);
  if (message_separator == std::string_view::npos) {
    return false;
  }

  parsed_log_record_s parsed;
  parsed.timestamp.assign(line.substr(0U, TIMESTAMP_SIZE));
  parsed.node_name.assign(node_name);
  parsed.thread_name.assign(line.substr(
      fields_begin,
      marker_position - fields_begin));
  parsed.level = selected_marker->level;
  if (!parse_source_location(
      line.substr(location_begin, message_separator - location_begin),
      parsed.source_file,
      parsed.source_line))
  {
    return false;
  }
  parsed.message.assign(line.substr(
      message_separator + FIELD_SEPARATOR.size()));
  parsed.evidence_id.assign(evidence_id);
  record = parsed;
  return true;
}

log_reader_c::log_reader_c(const ai_diagnostics_options_s & options)
: m_log_directory(options.log_directory.c_str()),
  m_cursor_path(m_log_directory / options.cursor_filename.c_str()),
  m_minimum_level(options.minimum_log_level),
  m_max_records_per_batch(options.max_records_per_batch),
  m_maximum_context_bytes(options.maximum_context_bytes),
  m_offsets()
{
  const std::filesystem::path cursor_filename(
    options.cursor_filename.c_str());
  if (m_log_directory.empty() || !m_log_directory.is_absolute() ||
    options.cursor_filename.empty() || m_max_records_per_batch == 0U ||
    m_maximum_context_bytes == 0U || options.max_log_files == 0U ||
    cursor_filename != cursor_filename.filename() ||
    cursor_filename == "." || cursor_filename == "..")
  {
    throw std::invalid_argument("AI diagnostic log reader options are invalid");
  }
  this->load_cursor();
}

bool log_reader_c::file_identity_s::operator<(
  const file_identity_s & other) const noexcept
{
  return device < other.device ||
         (device == other.device && inode < other.inode);
}

std::vector<log_reader_c::log_file_s>
log_reader_c::discover_log_files() const
{
  std::vector<log_file_s> files;
  std::error_code error;
  const std::filesystem::directory_iterator end;
  for (std::filesystem::directory_iterator iterator(m_log_directory, error);
    !error && iterator != end;
    iterator.increment(error))
  {
    const std::filesystem::directory_entry & entry = *iterator;
    if (entry.is_symlink(error) || error ||
      !entry.is_regular_file(error) || error ||
      !is_log_filename(entry.path()))
    {
      error.clear();
      continue;
    }
    file_identity_s identity;
    if (!read_file_identity(entry.path(), identity.device, identity.inode)) {
      continue;
    }
    files.push_back(log_file_s{
        entry.path(),
        identity,
        entry.last_write_time(error)});
    if (error) {
      files.pop_back();
      error.clear();
    }
  }
  std::sort(
    files.begin(),
    files.end(),
    [](const log_file_s & left, const log_file_s & right) {
      if (left.modified_at == right.modified_at) {
        return left.path < right.path;
      }
      return left.modified_at < right.modified_at;
    });
  return files;
}

void log_reader_c::load_cursor()
{
  std::ifstream cursor(m_cursor_path);
  file_identity_s identity;
  common::uint64_t offset = 0U;
  while (cursor >> identity.device >> identity.inode >> offset) {
    m_offsets[identity] = offset;
  }
}

void log_reader_c::save_cursor(
  const std::vector<log_file_s> & active_files) const
{
  const std::filesystem::path temporary_path =
    m_cursor_path.string() + ".tmp";
  std::ofstream cursor(temporary_path, std::ios::trunc);
  if (!cursor) {
    return;
  }
  for (const log_file_s & file : active_files) {
    const std::map<file_identity_s, common::uint64_t>::const_iterator position =
      m_offsets.find(file.identity);
    if (position != m_offsets.end()) {
      cursor << file.identity.device << ' ' << file.identity.inode << ' ' <<
        position->second << '\n';
    }
  }
  cursor.close();
  if (!cursor) {
    return;
  }
  std::error_code error;
  std::filesystem::rename(temporary_path, m_cursor_path, error);
  if (error) {
    std::filesystem::remove(m_cursor_path, error);
    error.clear();
    std::filesystem::rename(temporary_path, m_cursor_path, error);
  }
}

std::vector<parsed_log_record_s> log_reader_c::read_next_batch()
{
  std::vector<parsed_log_record_s> records;
  records.reserve(m_max_records_per_batch);
  std::size_t context_bytes = 0U;
  const std::vector<log_file_s> files = this->discover_log_files();

  for (const log_file_s & file : files) {
    if (records.size() >= m_max_records_per_batch) {
      break;
    }
    std::ifstream input(file.path);
    if (!input) {
      continue;
    }
    common::uint64_t & offset = m_offsets[file.identity];
    input.seekg(static_cast<std::streamoff>(offset));
    if (!input) {
      input.clear();
      offset = 0U;
      input.seekg(0);
    }

    std::string line;
    while (records.size() < m_max_records_per_batch) {
      const std::streampos line_start = input.tellg();
      if (!std::getline(input, line)) {
        break;
      }
      // A writer may still be appending the final record. Do not advance the
      // cursor until the line delimiter has been observed on a later pass.
      if (input.eof()) {
        break;
      }
      const std::streampos next_position = input.tellg();
      std::error_code size_error;
      const std::uintmax_t file_size = std::filesystem::file_size(
        file.path,
        size_error);
      const common::uint64_t candidate_offset =
        next_position != std::streampos(-1) ?
        static_cast<common::uint64_t>(
        static_cast<std::streamoff>(next_position)) :
        (size_error ?
        offset + static_cast<common::uint64_t>(line.size()) + 1U :
        static_cast<common::uint64_t>(file_size));
      const std::string node_name = node_name_from_path(file.path);
      const std::string evidence_id = node_name + ":" +
        std::to_string(file.identity.inode) + ":" +
        std::to_string(static_cast<common::uint64_t>(
            static_cast<std::streamoff>(line_start)));
      parsed_log_record_s record;
      if (!parse_log_line(node_name, evidence_id, line, record) ||
        record.level < m_minimum_level)
      {
        offset = candidate_offset;
        continue;
      }
      if (line.size() > m_maximum_context_bytes - context_bytes) {
        break;
      }
      records.push_back(record);
      context_bytes += line.size();
      offset = candidate_offset;
    }
  }

  std::sort(
    records.begin(),
    records.end(),
    [](const parsed_log_record_s & left, const parsed_log_record_s & right) {
      if (left.timestamp == right.timestamp) {
        return left.evidence_id.view() < right.evidence_id.view();
      }
      return left.timestamp.view() < right.timestamp.view();
    });
  this->save_cursor(files);
  return records;
}

}  // namespace ai_diagnostics
