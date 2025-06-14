#pragma once

#include <memory>
#include <string>
#include <qpdf/QPDF.hh>

namespace qpdf_ruby {

/**
 * A thin RAII wrapper around std::shared_ptr<QPDF>.
 *
 * - Only one QPDF parse per PDF file.
 * - Shared among Ruby proxy objects (StructureProxy, PathsProxy, …).
 * - Non-copyable but movable.
 */
class DocumentHandle final {
 public:
  // ---- factory ----------------------------------------------------------
  static std::unique_ptr<DocumentHandle> open(const std::string& filename);
  static std::unique_ptr<DocumentHandle> open_memory(std::string const& description, std::vector<unsigned char> data,
                                                     std::string const& password = "");

  // ---- public API -------------------------------------------------------
  /** Write the (possibly-modified) PDF to disk. */
  void write(const std::string& out_filename);

  std::string write_to_memory();

  /** Direct access for C++ helpers that need the raw QPDF. */
  QPDF& qpdf() { return *m_qpdf; }
  const QPDF& qpdf() const { return *m_qpdf; }

  // ---- rule of five -----------------------------------------------------
  ~DocumentHandle() = default;
  DocumentHandle(const DocumentHandle&) = delete;
  DocumentHandle& operator=(const DocumentHandle&) = delete;
  DocumentHandle(DocumentHandle&&) noexcept = default;
  DocumentHandle& operator=(DocumentHandle&&) noexcept = default;

 private:
  explicit DocumentHandle(std::shared_ptr<QPDF> qpdf);

  std::shared_ptr<QPDF> m_qpdf;
  std::vector<unsigned char> m_owned_buf;
};

extern "C" {
/** Returns a freshly allocated handle or nullptr on error (see errno). */
DocumentHandle* qpdf_ruby_open(const char* filename);

DocumentHandle* qpdf_ruby_open_memory(char const* desc, unsigned char const* buf, size_t len, char const* pwd);

/** Writes PDF; returns 0 on success, -1 on error (see errno). */
int qpdf_ruby_write(DocumentHandle* handle, const char* out_filename);

/** Deletes the handle (idempotent). */
void qpdf_ruby_close(DocumentHandle* handle);
}

}  // namespace qpdf_ruby
