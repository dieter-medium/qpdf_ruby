#define POINTERHOLDER_TRANSITION 1

#include "document_handle.hpp"

#include <qpdf/QPDFWriter.hh>
#include <qpdf/QPDF.hh>
#include <stdexcept>
#include <system_error>
#include <cerrno>

using namespace qpdf_ruby;

std::unique_ptr<DocumentHandle> DocumentHandle::open(const std::string& filename, std::string const& pwd) {
  auto qpdf = std::make_shared<QPDF>();
  try {
    qpdf->processFile(filename.c_str(), pwd.empty() ? nullptr : pwd.c_str());
  } catch (const QPDFExc& qex) {
    throw std::runtime_error(std::string("qpdf_ruby: failed to open \"") + filename + "\": " + qex.what() +
                             " (code: " + std::to_string(qex.getErrorCode()) + ")");
  } catch (const std::exception& ex) {
    throw std::runtime_error(std::string("qpdf_ruby: failed to open “") + filename + "”: " + ex.what());
  }
  return std::unique_ptr<DocumentHandle>(new DocumentHandle(qpdf));
}

std::unique_ptr<DocumentHandle> DocumentHandle::open_memory(std::string const& desc, std::vector<unsigned char> buf,
                                                            std::string const& pwd) {
  auto qpdf = std::make_shared<QPDF>();
  try {
    qpdf->processMemoryFile(desc.c_str(), reinterpret_cast<char const*>(buf.data()), buf.size(),
                            pwd.empty() ? nullptr : pwd.c_str());
  } catch (std::exception const& ex) {
    throw std::runtime_error("qpdf_ruby: open_memory failed: " + std::string(ex.what()));
  }

  auto h = std::unique_ptr<DocumentHandle>(new DocumentHandle(qpdf));
  h->m_owned_buf = std::move(buf);  // keep bytes alive
  return h;
}

DocumentHandle::DocumentHandle(std::shared_ptr<QPDF> qpdf) : m_qpdf(std::move(qpdf)) {}

void DocumentHandle::write(const std::string& out_filename) {
  try {
    // honour original file’s extension-level features (linearized? encrypted? …)
    QPDFWriter w(*m_qpdf, out_filename.c_str());
    w.setStaticID(true);  // deterministic IDs – helps tests
    setup_encryption(w);
    w.write();
  } catch (const std::exception& ex) {
    throw std::runtime_error(std::string("qpdf_ruby: failed to write “") + out_filename + "”: " + ex.what());
  }
}

std::string DocumentHandle::write_to_memory() {
  try {
    QPDFWriter w(*m_qpdf, nullptr);
    w.setStaticID(true);

    setup_encryption(w);

    w.setOutputMemory();
    w.write();

    auto b = w.getBuffer();
    return std::string(reinterpret_cast<char const*>(b->getBuffer()), b->getSize());
  } catch (std::exception const& ex) {
    throw std::runtime_error("qpdf_ruby: write_to_memory failed: " + std::string(ex.what()));
  }
}

void DocumentHandle::setup_encryption(QPDFWriter& w) const {
  if (!m_encryption_requested) return;

  switch (m_encrypt_R) {
    case 4:
      w.setR4EncryptionParametersInsecure(m_user_pw.c_str(), m_owner_pw.c_str(), m_encrypt_accessibility,
                                          m_encrypt_allow_extract, m_encrypt_assemble, m_encrypt_annotate_and_form,
                                          m_encrypt_form_filling, m_encrypt_allow_modify, m_encrypt_allow_print,
                                          m_encrypt_encrypt_metadata, m_encrypt_use_aes);
      break;
    case 5:
      w.setR5EncryptionParameters(m_user_pw.c_str(), m_owner_pw.c_str(), m_encrypt_accessibility,
                                  m_encrypt_allow_extract, m_encrypt_assemble, m_encrypt_annotate_and_form,
                                  m_encrypt_form_filling, m_encrypt_allow_modify, m_encrypt_allow_print,
                                  m_encrypt_encrypt_metadata);
      break;
    case 6:
      w.setR6EncryptionParameters(m_user_pw.c_str(), m_owner_pw.c_str(), m_encrypt_accessibility,
                                  m_encrypt_allow_extract, m_encrypt_assemble, m_encrypt_annotate_and_form,
                                  m_encrypt_form_filling, m_encrypt_allow_modify, m_encrypt_allow_print,
                                  m_encrypt_encrypt_metadata);
      break;
    default:
      throw std::runtime_error("Unsupported encryption revision (R): Only 4, 5, 6 are supported.");
  }
}

void DocumentHandle::set_encryption(const std::string& user_pw, const std::string& owner_pw, int R,
                                    qpdf_r3_print_e allow_print, bool allow_modify, bool allow_extract,
                                    bool accessibility, bool assemble, bool annotate_and_form, bool form_filling,
                                    bool encrypt_metadata, bool use_aes) {
  m_user_pw = user_pw;
  m_owner_pw = owner_pw;
  m_encrypt_R = R;
  m_encrypt_allow_print = allow_print;
  m_encrypt_allow_modify = allow_modify;
  m_encrypt_allow_extract = allow_extract;
  m_encrypt_accessibility = accessibility;
  m_encrypt_assemble = assemble;
  m_encrypt_annotate_and_form = annotate_and_form;
  m_encrypt_form_filling = form_filling;
  m_encrypt_encrypt_metadata = encrypt_metadata;
  m_encrypt_use_aes = use_aes;
  m_encryption_requested = true;
}

// ------------------------- C bridge impl --------------------------------

extern "C" {
DocumentHandle* qpdf_ruby_open(const char* filename, char const* pwd) {
  return DocumentHandle::open(filename, pwd ? pwd : "").release();
}

DocumentHandle* qpdf_ruby_open_memory(char const* desc, unsigned char const* buf, size_t len, char const* pwd) {
  std::vector<unsigned char> copy(buf, buf + len);  // simple ownership
  return DocumentHandle::open_memory(desc, std::move(copy), pwd ? pwd : "").release();
}

int qpdf_ruby_write(DocumentHandle* handle, const char* out_filename) {
  if (!handle) {
    errno = EBADF;
    return -1;
  }

  handle->write(out_filename);
  return 0;
}

void qpdf_ruby_set_encryption(DocumentHandle* handle, const char* user_pw, const char* owner_pw, int R,
                              qpdf_r3_print_e allow_print, int allow_modify, int allow_extract, int accessibility,
                              int assemble, int annotate_and_form, int form_filling, int encrypt_metadata,
                              int use_aes) {
  if (!handle) return;

  handle->set_encryption(std::string(user_pw ? user_pw : ""), std::string(owner_pw ? owner_pw : ""), R, allow_print,
                         allow_modify != 0, allow_extract != 0, accessibility != 0, assemble != 0,
                         annotate_and_form != 0, form_filling != 0, encrypt_metadata != 0, use_aes != 0);
}

void qpdf_ruby_close(DocumentHandle* handle) {
  delete handle;  // ok on nullptr
}
}  // extern "C"
