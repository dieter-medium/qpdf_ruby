# Debian trixie (the base of ruby:4.0) ships QPDF 12 - no testing/unstable pinning needed.
# Two stages: the gem's C++ extension is compiled with build-essential and libqpdf-dev in the first;
# the image keeps only the QPDF runtime library, the qpdf CLI and the installed gem.
FROM ruby:4.0-slim AS build

ENV DEBIAN_FRONTEND=noninteractive

RUN set -eux; \
    apt-get update; \
    apt-get -y --no-install-recommends install build-essential libqpdf-dev; \
    apt-get clean; rm -rf /var/lib/apt/lists/*

COPY ./pkg/qpdf_ruby-*.gem /tmp/

RUN gem install --no-document /tmp/qpdf_ruby-*.gem

FROM ruby:4.0-slim

ENV DEBIAN_FRONTEND=noninteractive

RUN set -eux; \
    apt-get update; \
    apt-get -y --no-install-recommends install qpdf libqpdf30; \
    apt-get clean; rm -rf /var/lib/apt/lists/*

# Smoke-test (shown in build log)
RUN qpdf --version  # should print "qpdf version 12.x"

# GEM_HOME of the ruby images: the gem and its compiled extension.
COPY --from=build /usr/local/bundle /usr/local/bundle
COPY ./spec/fixtures/example_accessibility.pdf /tmp/dummy.pdf

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
