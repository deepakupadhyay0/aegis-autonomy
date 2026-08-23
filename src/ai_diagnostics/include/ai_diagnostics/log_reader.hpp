#pragma once

#include "ai_diagnostics/ai_diagnostics_config.hpp"
#include "ai_diagnostics/visibility_control.hpp"
#include "common/fixed_string.hpp"
#include "common/numeric_types.hpp"
#include "logging/log_types.hpp"

#include <cstddef>
#include <filesystem>
#include <map>
#include <vector>

namespace ai_diagnostics
{

struct parsed_log_record_s
{
  common::string32_t timestamp;
  common::string64_t node_name;
  common::string16_t thread_name;
  logging::log_level_e level{logging::log_level_e::info};
  common::string64_t source_file;
  common::uint32_t source_line{0U};
  common::string256_t message;
  common::string128_t evidence_id;
};

class AI_DIAGNOSTICS_PUBLIC log_reader_c final
{
public:
  explicit log_reader_c(const ai_diagnostics_options_s & options);
  ~log_reader_c() noexcept = default;

  log_reader_c(const log_reader_c &) = delete;
  log_reader_c & operator=(const log_reader_c &) = delete;
  log_reader_c(log_reader_c &&) = delete;
  log_reader_c & operator=(log_reader_c &&) = delete;

  std::vector<parsed_log_record_s> read_next_batch();

private:
  struct file_identity_s
  {
    common::uint64_t device{0U};
    common::uint64_t inode{0U};

    bool operator<(const file_identity_s & other) const noexcept;
  };

  struct log_file_s
  {
    std::filesystem::path path;
    file_identity_s identity;
    std::filesystem::file_time_type modified_at;
  };

  std::vector<log_file_s> discover_log_files() const;
  void load_cursor();
  void save_cursor(const std::vector<log_file_s> & active_files) const;

  std::filesystem::path m_log_directory;
  std::filesystem::path m_cursor_path;
  logging::log_level_e m_minimum_level;
  std::size_t m_max_records_per_batch;
  std::size_t m_maximum_context_bytes;
  std::map<file_identity_s, common::uint64_t> m_offsets;
};

AI_DIAGNOSTICS_PUBLIC bool parse_log_line(
  const std::string_view node_name,
  const std::string_view evidence_id,
  const std::string_view line,
  parsed_log_record_s & record) noexcept;

}  // namespace ai_diagnostics
