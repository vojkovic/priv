FROM ghcr.io/void-linux/void-glibc:latest AS build
RUN xbps-install -Suy xbps && xbps-install -uy && \
    sed -i '/gconv/d' /etc/xbps.d/noextract.conf && xbps-install -fy glibc && \
    xbps-install -y bash gcc make binutils tar gzip diffutils findutils pkg-config curl git ca-certificates \
        libxml2-devel zlib-devel liblzma-devel
WORKDIR /src
ARG VERSION=
COPY . .
RUN make ${VERSION:+VERSION=$VERSION} -j"$(nproc)" && strip priv && \
    sed 's/^listen = .*/listen = "0.0.0.0"/' config.toml > /tmp/config.toml

RUN mkdir -p /rootfs/cache /rootfs/etc/priv /rootfs/etc/ssl/certs /rootfs/usr/lib && \
    ldd priv | awk '{for (i = 1; i <= NF; i++) if ($i ~ /^\//) print $i}' | sort -u | \
        xargs -I{} cp -L --parents {} /rootfs && \
    cp -r /usr/lib/gconv /rootfs/usr/lib/gconv && \
    cp -L /etc/ssl/certs/ca-certificates.crt /rootfs/etc/ssl/certs/ && \
    cp priv /rootfs/priv && cp /tmp/config.toml /rootfs/etc/priv/config.toml && \
    chown -R 65534:65534 /rootfs/cache

FROM scratch
COPY --from=build /rootfs /
ENV XDG_CACHE_HOME=/cache
USER 65534:65534
EXPOSE 8888
ENTRYPOINT ["/priv"]
