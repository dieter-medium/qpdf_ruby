# Debian trixie (the base of ruby:4.0) ships QPDF 12 - no testing/unstable pinning needed.
FROM ruby:4.0-slim

ENV DEBIAN_FRONTEND=noninteractive

RUN set -eux; \
    apt-get update; \
    apt-get -y --no-install-recommends install build-essential qpdf libqpdf-dev; \
    apt-get clean; rm -rf /var/lib/apt/lists/*

# Smoke-test (shown in build log)
RUN qpdf --version  # should print "qpdf version 12.x"

COPY ./pkg/qpdf_ruby-*.gem ./
COPY ./spec/fixtures/example_accessibility.pdf /tmp/dummy.pdf

RUN gem install ./qpdf_ruby-*.gem

RUN ruby -r qpdf_ruby -e 'puts "QpdfRuby version: #{QpdfRuby::VERSION}"; doc = QpdfRuby::Document.new "/tmp/dummy.pdf"; puts doc.show_structure'

# Create a non-root user
RUN set -eux; \
    groupadd -r appuser && useradd -r -g appuser -m -d /home/appuser appuser


RUN set -eux; \
    mkdir -p /workspace

# Set working directory
WORKDIR /workspace

RUN chown -R appuser:appuser /workspace

# Switch to non-root user
USER appuser

CMD ["/usr/bin/bash"]

