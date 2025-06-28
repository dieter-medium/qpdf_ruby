#pragma once

#define POINTERHOLDER_TRANSITION 1

#include <memory>
#include <string>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFWriter.hh>

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
  static std::unique_ptr<DocumentHandle> open(const std::string& filename, std::string const& pwd);
  static std::unique_ptr<DocumentHandle> open_memory(std::string const& description, std::vector<unsigned char> data,
                                                     std::string const& password = "");

  // ---- public API -------------------------------------------------------
  /** Write the (possibly-modified) PDF to disk. */
  void write(const std::string& out_filename);

  std::string write_to_memory();

  void set_encryption(const std::string& user_pw, const std::string& owner_pw, int R, qpdf_r3_print_e allow_print,
                      bool allow_modify, bool allow_extract, bool accessibility = true, bool assemble = true,
                      bool annotate_and_form = true, bool form_filling = true, bool encrypt_metadata = true,
                      bool use_aes = true);
  void setup_encryption(QPDFWriter& w) const;

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

  // --- New encryption settings ---
  bool m_encryption_requested = false;
  std::string m_user_pw;
  std::string m_owner_pw;
  int m_encrypt_R = 4;
  qpdf_r3_print_e m_encrypt_allow_print{qpdf_r3p_full};
  bool m_encrypt_allow_modify = true;
  bool m_encrypt_allow_extract = true;
  bool m_encrypt_accessibility = true;
  bool m_encrypt_assemble = true;
  bool m_encrypt_annotate_and_form = true;
  bool m_encrypt_form_filling = true;
  bool m_encrypt_encrypt_metadata = true;
  bool m_encrypt_use_aes = true;
};

extern "C" {
/** Returns a freshly allocated handle or nullptr on error (see errno). */
DocumentHandle* qpdf_ruby_open(const char* filename, char const* pwd);

DocumentHandle* qpdf_ruby_open_memory(char const* desc, unsigned char const* buf, size_t len, char const* pwd);

/** Writes PDF; returns 0 on success, -1 on error (see errno). */
int qpdf_ruby_write(DocumentHandle* handle, const char* out_filename);

/** Deletes the handle (idempotent). */
void qpdf_ruby_close(DocumentHandle* handle);
}

void qpdf_ruby_set_encryption(struct DocumentHandle* handle, const char* user_pw, const char* owner_pw, int R,
                              qpdf_r3_print_e allow_print,
                              int allow_modify,  // bool as int (0/1)
                              int allow_extract, int accessibility, int assemble, int annotate_and_form,
                              int form_filling, int encrypt_metadata, int use_aes);

}  // namespace qpdf_ruby
