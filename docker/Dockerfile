FROM ruby:3.3

ENV DEBIAN_FRONTEND=noninteractive

RUN set -eux; \
    echo "deb http://deb.debian.org/debian testing main"   >  /etc/apt/sources.list.d/extra.list; \
    echo "deb http://deb.debian.org/debian unstable main" >> /etc/apt/sources.list.d/extra.list; \
    \
    printf "Package: *\nPin: release a=testing\nPin-Priority: 100\n\n"   >  /etc/apt/preferences.d/99-pin-testing; \
    printf "Package: *\nPin: release a=unstable\nPin-Priority: 100\n"  >> /etc/apt/preferences.d/99-pin-testing; \
    \
    apt-get update; \
    apt-get -y --no-install-recommends install qpdf libqpdf-dev -t testing; \
    apt-mark hold qpdf libqpdf-dev; \
    \
    apt-get clean; rm -rf /var/lib/apt/lists/*;

# Smoke‑test (shown in build log)
RUN qpdf --version  # should print "qpdf version 12.x"


COPY ./pkg/qpdf_ruby-*.gem ./
COPY ./spec/fixtures/example_accessibility.pdf /tmp/dummy.pdf

RUN gem install  ./qpdf_ruby-*.gem -- --with-qpdf-include=/usr/local/include/qpdf --with-qpdf-lib=/usr/local/lib

RUN ruby -r qpdf_ruby -e 'puts "QpdfRuby version: #{QpdfRuby::VERSION}";doc = QpdfRuby::Document.new "/tmp/dummy.pdf"; puts doc.show_structure'

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

