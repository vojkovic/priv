FROM alpine:3.22 AS build
RUN apk add --no-cache build-base curl pkgconf libxml2-dev libxml2-static zlib-static xz-static ca-certificates git
WORKDIR /src
ARG VERSION=
COPY . .
RUN make STATIC=1 ${VERSION:+VERSION=$VERSION} -j"$(nproc)" && strip priv && mkdir -p /cache && \
    sed 's/^listen = .*/listen = "0.0.0.0"/' config.toml > /tmp/config.toml

FROM build AS tools
RUN apk add --no-cache clang20-extra-tools cppcheck

FROM tools AS check
RUN make format-check lint

FROM scratch
COPY --from=build /etc/ssl/certs/ca-certificates.crt /etc/ssl/certs/ca-certificates.crt
COPY --from=build --chown=65534:65534 /cache /cache
COPY --from=build /src/priv /priv
COPY --from=build /tmp/config.toml /etc/priv/config.toml
ENV XDG_CACHE_HOME=/cache
USER 65534:65534
EXPOSE 8888
ENTRYPOINT ["/priv"]
