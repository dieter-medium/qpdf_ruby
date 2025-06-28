# frozen_string_literal: true

require_relative "qpdf_ruby/version"
require_relative "qpdf_ruby/qpdf_ruby"

module QpdfRuby
  ENCRYPTION_REVISION_AES_128  = 4  # Acrobat 6.x, 128-bit AES
  ENCRYPTION_REVISION_AES_256  = 5  # Acrobat 9.x, 256-bit AES
  ENCRYPTION_REVISION_AES_256U = 6  # Acrobat X, 256-bit AES (PDF 2.0+ update)

  class Error < StandardError; end
  # Your code goes here...
end
