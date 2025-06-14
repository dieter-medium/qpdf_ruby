#include "document_handle.hpp"

#include <qpdf/QPDFWriter.hh>
#include <stdexcept>
#include <system_error>
#include <cerrno>

using namespace qpdf_ruby;

std::unique_ptr<DocumentHandle> DocumentHandle::open(const std::string& filename) {
  auto qpdf = std::make_shared<QPDF>();
  try {
    // Use empty password → owner & user pwd same.
    qpdf->processFile(filename.c_str());
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
    w.write();
  } catch (const std::exception& ex) {
    throw std::runtime_error(std::string("qpdf_ruby: failed to write “") + out_filename + "”: " + ex.what());
  }
}

std::string DocumentHandle::write_to_memory() {
  try {
    QPDFWriter w(*m_qpdf, nullptr);
    w.setStaticID(true);
    w.setOutputMemory();
    w.write();

    auto b = w.getBuffer();
    return std::string(reinterpret_cast<char const*>(b->getBuffer()), b->getSize());
  } catch (std::exception const& ex) {
    throw std::runtime_error("qpdf_ruby: write_to_memory failed: " + std::string(ex.what()));
  }
}

// ------------------------- C bridge impl --------------------------------

extern "C" {
DocumentHandle* qpdf_ruby_open(const char* filename) {
  try {
    return DocumentHandle::open(filename).release();
  } catch (const std::exception&) {
    errno = EIO;
    return nullptr;
  }
}

DocumentHandle* qpdf_ruby_open_memory(char const* desc, unsigned char const* buf, size_t len, char const* pwd) {
  try {
    std::vector<unsigned char> copy(buf, buf + len);  // simple ownership
    return DocumentHandle::open_memory(desc, std::move(copy), pwd ? pwd : "").release();
  } catch (...) {
    errno = EIO;
    return nullptr;
  }
}

int qpdf_ruby_write(DocumentHandle* handle, const char* out_filename) {
  if (!handle) {
    errno = EBADF;
    return -1;
  }
  try {
    handle->write(out_filename);
    return 0;
  } catch (const std::exception&) {
    errno = EIO;
    return -1;
  }
}

void qpdf_ruby_close(DocumentHandle* handle) {
  delete handle;  // ok on nullptr
}
}  // extern "C"
